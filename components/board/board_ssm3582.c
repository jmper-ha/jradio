#include "board_ssm3582.h"

#include "board_i2c.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "ssm3582.h"

static const char *TAG = "ssm3582";

/* 100 kHz, as the tuner and the clock sit on the same bus: a handful of
 * writes at boot and one per start and stop has no use for more. */
#define SSM3582_I2C_HZ 100000U
#define SSM3582_I2C_TIMEOUT_MS 50
/* A few tries per transfer, as the tuner and the clock make: on the bench the
 * I2S bit clock beside the I2C wires spoiled about one tuner read a second. */
#define SSM3582_I2C_TRIES 3
/* After S_RST, before the first write: the datasheet gives the chip ~10 ms
 * from DVDD to operational, and a reset is that start again. */
#define SSM3582_RESET_SETTLE_MS 10

static i2c_master_dev_handle_t s_device;
/* Boot runs in app_main; after that the power and the rate are changed under
 * the audio mutex, which already keeps them from overlapping - the lock is
 * for the write and its log line to stay one thing if that ever changes. */
static SemaphoreHandle_t s_lock;
static bool s_present;
static bool s_powered;
static uint8_t s_dac_ctrl = 0xFFU;

static esp_err_t write_register(uint8_t reg, uint8_t value)
{
    const uint8_t bytes[2] = {reg, value};
    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < SSM3582_I2C_TRIES && err != ESP_OK; ++attempt) {
        err = i2c_master_transmit(s_device, bytes, sizeof(bytes), SSM3582_I2C_TIMEOUT_MS);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "write %02x <- %02x failed: %s", reg, value, esp_err_to_name(err));
    }
    return err;
}

static esp_err_t read_register(uint8_t reg, uint8_t *value)
{
    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < SSM3582_I2C_TRIES && err != ESP_OK; ++attempt) {
        err = i2c_master_transmit_receive(s_device, &reg, 1, value, 1, SSM3582_I2C_TIMEOUT_MS);
    }
    return err;
}

static esp_err_t write_rate(uint32_t sample_rate)
{
    const uint8_t word = ssm3582_dac_ctrl_word(sample_rate);
    if (word == 0xFFU) {
        ESP_LOGW(TAG, "%lu Hz is in none of the chip's rate bands", (unsigned long)sample_rate);
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!ssm3582_rate_takes_64fs(sample_rate)) {
        ESP_LOGW(TAG, "%lu Hz: the chip wants a bit clock over 64 x Fs below 32 kHz, "
                      "which is all the S3 sends - expect no sound",
                 (unsigned long)sample_rate);
    }
    if (word == s_dac_ctrl) return ESP_OK;
    const esp_err_t err = write_register(SSM3582_REG_DAC_CTRL, word);
    if (err == ESP_OK) s_dac_ctrl = word;
    return err;
}

static esp_err_t set_up(uint8_t gain_code, uint32_t sample_rate)
{
    uint8_t id[3] = {0};
    esp_err_t err = read_register(SSM3582_REG_VENDOR_ID, &id[0]);
    if (err == ESP_OK) err = read_register(SSM3582_REG_DEVICE_ID1, &id[1]);
    if (err == ESP_OK) err = read_register(SSM3582_REG_DEVICE_ID2, &id[2]);
    if (err != ESP_OK) return err;
    if (!ssm3582_identity_matches(id[0], id[1], id[2])) {
        ESP_LOGE(TAG, "the chip at this address is %02x %02x %02x, not an SSM3582", id[0],
                 id[1], id[2]);
        return ESP_ERR_NOT_FOUND;
    }
    /* From the reset values, whatever a previous boot left: the chip keeps
     * its registers through a reset of the S3 alone. */
    err = write_register(SSM3582_REG_SOFT_RESET, SSM3582_SOFT_RESET_WORD);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(SSM3582_RESET_SETTLE_MS));
    s_dac_ctrl = 0x02U;  // DAC_CTRL's reset value
    // Powered down first, and kept so until the first sound has a clock.
    err = write_register(SSM3582_REG_POWER_CTRL, ssm3582_power_ctrl_word(false));
    if (err == ESP_OK) err = write_register(SSM3582_REG_SAI_CTRL1, SSM3582_SAI_CTRL1_I2S);
    if (err == ESP_OK) {
        err = write_register(SSM3582_REG_AMP_DAC_CTRL, ssm3582_amp_dac_ctrl_word(gain_code));
    }
    if (err == ESP_OK) err = write_register(SSM3582_REG_VOL_LEFT_CTRL, SSM3582_VOLUME_0DB);
    if (err == ESP_OK) (void)write_rate(sample_rate);
    return err;
}

esp_err_t board_ssm3582_init(uint8_t address, uint8_t gain_code, uint32_t sample_rate)
{
    if (s_present) return ESP_OK;
    if (address < SSM3582_I2C_ADDRESS_MIN || address > SSM3582_I2C_ADDRESS_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) return ESP_ERR_NO_MEM;
    }
    i2c_master_bus_handle_t bus = NULL;
    esp_err_t err = board_i2c0_bus(&bus);
    if (err == ESP_OK && s_device == NULL) {
        const i2c_device_config_t config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = address,
            .scl_speed_hz = SSM3582_I2C_HZ,
        };
        err = i2c_master_bus_add_device(bus, &config, &s_device);
    }
    if (err == ESP_OK) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        err = set_up(gain_code, sample_rate);
        xSemaphoreGive(s_lock);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "no SSM3582 set up at 0x%02x: %s - the speakers stay silent", address,
                 esp_err_to_name(err));
        return err;
    }
    s_present = true;
    s_powered = false;
    ESP_LOGI(TAG, "SSM3582 at 0x%02x: stereo I2S at 64 x Fs, %u dB analogue gain", address,
             (unsigned)(gain_code == 1U ? 16U : gain_code == 2U ? 19U : gain_code == 3U ? 21U
                                                                                       : 13U));
    return ESP_OK;
}

bool board_ssm3582_present(void)
{
    return s_present;
}

void board_ssm3582_set_powered(bool powered)
{
    if (!s_present || powered == s_powered) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (write_register(SSM3582_REG_POWER_CTRL, ssm3582_power_ctrl_word(powered)) == ESP_OK) {
        s_powered = powered;
        ESP_LOGI(TAG, "amplifier %s", powered ? "on" : "powered down");
    }
    xSemaphoreGive(s_lock);
}

void board_ssm3582_set_rate(uint32_t sample_rate)
{
    if (!s_present) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    (void)write_rate(sample_rate);
    xSemaphoreGive(s_lock);
}
