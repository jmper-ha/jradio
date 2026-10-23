#include "ssm3582.h"

#include "board_i2c.h"
#include "board_options.h"
#include "esp_check.h"
#include "esp_log.h"
#include "ssm3582_chip.h"

static const char *TAG = "ssm3582";

/* 100 kHz, like the other two devices on this bus: nine bytes at setup
 * and a pair per change of volume or clock have no use for more. */
#define SSM3582_I2C_HZ 100000U
#define SSM3582_I2C_TIMEOUT_MS 100

/* Where on I2C0 to look for the chip: its default address, unless the board
 * names another one - the ADDRx pins pick it from sixteen at power-up, and
 * the probe below does not scan the bus for a chip nobody said where it
 * sits. */
#ifdef AUDIO_DAC_I2C_ADDRESS
#define SSM3582_ADDRESS AUDIO_DAC_I2C_ADDRESS
#else
#define SSM3582_ADDRESS SSM3582_I2C_ADDRESS_DEFAULT
#endif
_Static_assert(SSM3582_ADDRESS >= 0x08U && SSM3582_ADDRESS <= 0x77U,
               "AUDIO_DAC_I2C_ADDRESS has to be a seven-bit I2C address");

/* Where the knob's 100% sits on the chip's volume ladder, in whole dB,
 * from board_options.h. Left out it is unity - the top the curve has
 * always had, the chip's reset value; a board whose speakers want more
 * than the line can name a boost up to the chip's +24 dB, and one that
 * wants a quieter ceiling a negative figure. It is the digital path of
 * this chip that is being stretched here - its analog gain stays at the
 * quietest setting this driver writes - so the steps of the curve, and
 * what each percentage means against the next one, do not move. */
#ifdef AUDIO_DAC_MAX_VOLUME_DB
#define SSM3582_MAX_VOLUME_DB AUDIO_DAC_MAX_VOLUME_DB
#else
#define SSM3582_MAX_VOLUME_DB 0
#endif
_Static_assert(SSM3582_MAX_VOLUME_DB >= SSM3582_VOL_DB_QUIETEST &&
                   SSM3582_MAX_VOLUME_DB <= SSM3582_VOL_DB_LOUDEST,
               "AUDIO_DAC_MAX_VOLUME_DB has to sit inside the chip's volume ladder");

static i2c_master_dev_handle_t s_dev;
static bool s_playing;
/* The volume register as the chip holds it now, -1 until the first
 * write: a setting that has not moved does not spend the bus. */
static int s_volume_written = -1;

/* The register by name, so that the log of what goes over the bus reads
 * like something. */
static const char *reg_name(uint8_t reg)
{
    switch (reg) {
        case SSM3582_REG_POWER_CTRL: return "POWER_CTRL";
        case SSM3582_REG_DAC_CTRL: return "DAC_CTRL";
        case SSM3582_REG_AMP_DAC_CTRL: return "AMP_DAC_CTRL";
        case SSM3582_REG_VOL_LEFT: return "VOL_LEFT";
        case SSM3582_REG_SAI_CTRL1: return "SAI_CTRL1";
        case SSM3582_REG_SAI_CTRL2: return "SAI_CTRL2";
        case SSM3582_REG_SSA_ID: return "SSA_ID";
        default: return "unknown";
    }
}

static esp_err_t ssm3582_write(uint8_t reg, uint8_t value)
{
    ESP_LOGD(TAG, "0x%02X %s (0x%02X) <- 0x%02X", SSM3582_ADDRESS, reg_name(reg), reg,
             value);
    const uint8_t frame[2] = {reg, value};
    return i2c_master_transmit(s_dev, frame, sizeof(frame), SSM3582_I2C_TIMEOUT_MS);
}

esp_err_t ssm3582_setup(uint32_t sample_rate, uint8_t volume_percent)
{
    /* What goes over the bus to this chip is the interesting part of its
     * log, and the writes are few - setup, every volume step, every clock
     * change. The lines are compiled into the image already (sdkconfig has
     * CONFIG_LOG_MAXIMUM_LEVEL_DEBUG); this opens the gate at runtime. */
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
    i2c_master_bus_handle_t bus = NULL;
    ESP_RETURN_ON_ERROR(board_i2c0_bus(&bus), TAG, "no I2C0 for the SSM3582");
    if (s_dev == NULL) {
        const i2c_device_config_t config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = SSM3582_ADDRESS,
            .scl_speed_hz = SSM3582_I2C_HZ,
        };
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &config, &s_dev), TAG,
                            "add SSM3582 to I2C0 failed");
    }
    /* Anything that answers at the address is the chip: the register read
     * is the presence test, its content a factory scratch we ignore. */
    uint8_t id = 0U;
    const uint8_t probe = SSM3582_REG_SSA_ID;
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_dev, &probe, 1U, &id, 1U,
                                                   SSM3582_I2C_TIMEOUT_MS),
                        TAG, "no SSM3582 answering at 0x%02X", SSM3582_ADDRESS);
    ESP_LOGI(TAG, "SSM3582 on I2C0 at 0x%02X", SSM3582_ADDRESS);
    /* A ceiling other than unity is worth one line: it is a board
     * option, and a radio that is unexpectedly louder or quieter than
     * every other one should say which number of its own file did it. */
    if (SSM3582_MAX_VOLUME_DB != 0) {
        ESP_LOGI(TAG, "loudest setting at %d dB (volume register 0x%02X)",
                 SSM3582_MAX_VOLUME_DB,
                 ssm3582_db_to_volume_register(SSM3582_MAX_VOLUME_DB));
    }

    ESP_RETURN_ON_ERROR(ssm3582_write(SSM3582_REG_AMP_DAC_CTRL, (SSM3582_AMP_DAC_CTRL_LOW_PWR | SSM3582_AMP_DAC_CTRL_13DB)),
                        TAG, "set amplifier analog gain failed");

    /* The serial port first, before anything can reach it: out of reset
     * the chip sits in TDM mode with 32-bit slots, and this is what
     * makes it take the plain stereo I2S the S3 drives. */
    ESP_RETURN_ON_ERROR(ssm3582_write(SSM3582_REG_SAI_CTRL1, SSM3582_SAI_CTRL1_STEREO_I2S),
                        TAG, "set serial format failed");
    ESP_RETURN_ON_ERROR(ssm3582_write(SSM3582_REG_SAI_CTRL2, SSM3582_SAI_CTRL2_16_BIT_LINKED),
                        TAG, "set sample width failed");
    ssm3582_set_sample_rate(sample_rate);
    ssm3582_set_volume(volume_percent);
    /* Closed until a source starts - board_amp_drive() opens it, and the
     * chip's power-up takes 10 ms to the 93 ms of silent clock the first
     * start queues ahead of the first sample. */
    s_playing = false;
    ESP_RETURN_ON_ERROR(ssm3582_write(SSM3582_REG_POWER_CTRL, SSM3582_POWER_DOWN), TAG,
                        "power down failed");
    return ESP_OK;
}

bool ssm3582_ready(void)
{
    return s_dev != NULL;
}

void ssm3582_set_playing(bool play)
{
    if (s_dev == NULL || play == s_playing) return;
    const esp_err_t result = ssm3582_write(SSM3582_REG_POWER_CTRL,
                                           play ? SSM3582_POWER_UP : SSM3582_POWER_DOWN);
    if (result != ESP_OK) {
        /* Left closed by the chip, whatever the board thinks: the next
         * call will ask again. */
        ESP_LOGW(TAG, "amplifier power change failed: %s", esp_err_to_name(result));
        return;
    }
    s_playing = play;
    ESP_LOGI(TAG, "amplifier %s", play ? "open" : "closed");
}

void ssm3582_set_volume(uint8_t percent)
{
    if (s_dev == NULL) return;
    /* The curve is a count of steps under the board's ceiling; the
     * ceiling is the one constant board_options.h set for this part. */
    const uint8_t ceiling = ssm3582_db_to_volume_register(SSM3582_MAX_VOLUME_DB);
    const int value = ssm3582_volume_register(percent, ceiling);
    if (value == s_volume_written) return;
    const esp_err_t result = ssm3582_write(SSM3582_REG_VOL_LEFT, (uint8_t)value);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "set volume to %u%% failed: %s", (unsigned int)percent,
                 esp_err_to_name(result));
        return;
    }
    s_volume_written = value;
}

void ssm3582_set_sample_rate(uint32_t sample_rate)
{
    if (s_dev == NULL) return;
    const uint8_t fs = ssm3582_dac_fs_bits(sample_rate);
    if (fs == SSM3582_DAC_FS_NONE) {
        ESP_LOGW(TAG, "the SSM3582 has no sample-rate setting for %lu Hz",
                 (unsigned long)sample_rate);
        return;
    }
    /* The geometry this firmware drives is fixed - two 32-bit slots,
     * 64 x Fs - and it is short of the chip's clock floor below 32 kHz.
     * Saying so here is the difference between a silent speaker and a
     * line in the log naming the cause. */
    if (!ssm3582_bclk_rate_ok(sample_rate, SSM3582_BCLK_RATIO)) {
        ESP_LOGW(TAG, "the SSM3582 does not take %lu Hz at BCLK=%u x Fs - it will stay quiet",
                 (unsigned long)sample_rate, (unsigned int)SSM3582_BCLK_RATIO);
    }
    const esp_err_t result = ssm3582_write(SSM3582_REG_DAC_CTRL, fs);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "set sample rate %lu Hz failed: %s", (unsigned long)sample_rate,
                 esp_err_to_name(result));
    }
}