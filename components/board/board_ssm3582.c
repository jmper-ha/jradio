#include "board_ssm3582.h"

#include "board_i2c.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "ssm3582.h"

/* After the driver's own headers, as board_options.h requires. The wiring
 * says whether the amplifier is fitted and which address its straps select;
 * a build that leaves the switch out still compiles this file, and only the
 * start in app_main goes without it. */
#include "board_options.h"

static const char *TAG = "board_ssm3582";

/* The address as board_options.h straps it. The part answers anywhere in
 * 0x10..0x1F; a number outside that is a wiring typo worth a build error
 * rather than a device that never answers. */
#ifdef AMP_SSM3582_I2C_ADDRESS
#define AMP_I2C_ADDRESS AMP_SSM3582_I2C_ADDRESS
#else
#define AMP_I2C_ADDRESS SSM3582_I2C_ADDRESS
#endif
#if AMP_I2C_ADDRESS < SSM3582_I2C_ADDRESS_MIN || AMP_I2C_ADDRESS > SSM3582_I2C_ADDRESS_MAX
#error "AMP_SSM3582_I2C_ADDRESS must be 0x10..0x1F - what ADDR0/ADDR1 can strap"
#endif

/* 100 kHz, as the RTC and the tuner sit: nothing here writes often enough to
 * want more, and the wiring is the same wires with the same pull-ups. */
#define AMP_I2C_HZ 100000U
#define AMP_I2C_TIMEOUT_MS 50

/* Each transfer is tried a few times before it counts as failed, the same
 * deal as the tuner's: the bus is shared and long, and one spoiled transfer
 * out of three is a wire's problem, not the chip's. */
#define AMP_I2C_TRIES 3

/* After the soft reset, before the chip takes its configuration: the reset
 * runs itself. Too short and the first writes are swallowed. */
#define AMP_RESET_SETTLE_MS 5

static i2c_master_dev_handle_t s_device;
static SemaphoreHandle_t s_lock;
// What has been written, for repair and for callers that ask without a read.
static uint8_t s_dac_ctrl;
static uint8_t s_power_ctrl;
static uint8_t s_volume = SSM3582_VOLUME_MAX_PERCENT;
// The gate: closed until the board says sound is actually flowing (and at
// boot, so a boot is quiet whatever the chip's own reset leaves). The knob's
// zero is the second input; both go through ssm3582_should_mute().
static bool s_gate_closed = true;
static bool s_muted = true;
static bool s_powered;
static bool s_present;

/* Held for a whole call: an init or a repair is a sequence a volume write
 * must not land in the middle of. */
#define LOCKED(call)                                                              \
    do {                                                                          \
        ESP_RETURN_ON_FALSE(s_device != NULL, ESP_ERR_INVALID_STATE, TAG,         \
                            "no amplifier");                                      \
        xSemaphoreTake(s_lock, portMAX_DELAY);                                    \
        const esp_err_t locked_result = (call);                                   \
        xSemaphoreGive(s_lock);                                                   \
        return locked_result;                                                     \
    } while (0)

static esp_err_t write_register(uint8_t reg, uint8_t value)
{
    const uint8_t bytes[2] = {reg, value};
    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < AMP_I2C_TRIES && err != ESP_OK; ++attempt) {
        err = i2c_master_transmit(s_device, bytes, sizeof(bytes), AMP_I2C_TIMEOUT_MS);
        if (err != ESP_OK) {
            ESP_LOGD(TAG, "write %02x <- %02x try %d failed: %s", reg, value, attempt,
                     esp_err_to_name(err));
        }
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "write %02x <- %02x failed: %s", reg, value, esp_err_to_name(err));
    } else {
        ESP_LOGD(TAG, "write %02x <- %02x", reg, value);
    }
    return err;
}

static esp_err_t read_register(uint8_t reg, uint8_t *value)
{
    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < AMP_I2C_TRIES && err != ESP_OK; ++attempt) {
        err = i2c_master_transmit_receive(s_device, &reg, 1, value, 1, AMP_I2C_TIMEOUT_MS);
        if (err != ESP_OK) {
            ESP_LOGD(TAG, "read %02x try %d failed: %s", reg, attempt, esp_err_to_name(err));
        }
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "read %02x failed: %s", reg, esp_err_to_name(err));
    } else {
        ESP_LOGD(TAG, "read %02x -> %02x", reg, *value);
    }
    return err;
}

/* What init wrote, as one word each - the state this driver keeps, which is
 * also what repair() writes back over whatever a spoiled transfer left. */
static esp_err_t write_dac_ctrl(void) { return write_register(SSM3582_REG_DAC_CTRL, s_dac_ctrl); }
static esp_err_t write_power_ctrl(void)
{
    return write_register(SSM3582_REG_POWER_CTRL, s_power_ctrl);
}

static esp_err_t write_volumes(uint8_t code)
{
    ESP_RETURN_ON_ERROR(write_register(SSM3582_REG_VOL_LEFT_CTRL, code), TAG, "volume left");
    return write_register(SSM3582_REG_VOL_RIGHT_CTRL, code);
}

/* The proven register table, in its order - nothing else: no soft reset and
 * no words of this driver's own. What the table leaves (DAC control, power)
 * is remembered as the state the driver keeps for repair() and for the mute
 * bits it later flips. */
static esp_err_t configure(void)
{
    for (size_t i = 0U; i < SSM3582_INIT_WRITES; ++i) {
        const ssm3582_reg_write_t *entry = &ssm3582_init_writes[i];
        ESP_RETURN_ON_ERROR(write_register(entry->reg, entry->value), TAG, "init");
    }
    /* 06h as the table wrote it: both channels passing. The gate is closed
     * all the same - the first apply_mute() after the caller's volume push
     * sets the mute bits, before any clock is handed to the chip. */
    s_dac_ctrl = SSM3582_DAC_CTRL_INIT;
    s_power_ctrl = SSM3582_POWER_CTRL_INIT;
    s_muted = false;
    s_powered = true;
    return ESP_OK;
}

esp_err_t board_ssm3582_init(void)
{
    if (s_device != NULL) return ESP_OK;
    /* The driver's register traffic is at DEBUG, and this tag alone is raised
     * to see it: the default level is INFO, and turning the whole log up to
     * watch one chip buries everything else the monitor is there for. Every
     * write and read the chip gets shows from here on - `idf.py monitor`. */
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock != NULL, ESP_ERR_NO_MEM, TAG, "lock");

    i2c_master_bus_handle_t bus = NULL;
    ESP_RETURN_ON_ERROR(board_i2c0_bus(&bus), TAG, "bus");
    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AMP_I2C_ADDRESS,
        .scl_speed_hz = AMP_I2C_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &device_config, &s_device), TAG, "device");

    /* Probe by identity, not by an ACK: something else at 0x10 that lets a
     * byte through is a board to report, not to play through. */
    uint8_t vendor = 0U;
    uint8_t device1 = 0U;
    uint8_t device2 = 0U;
    esp_err_t probed = read_register(SSM3582_REG_VENDOR_ID, &vendor);
    if (probed == ESP_OK) probed = read_register(SSM3582_REG_DEVICE_ID1, &device1);
    if (probed == ESP_OK) probed = read_register(SSM3582_REG_DEVICE_ID2, &device2);
    if (probed != ESP_OK || !ssm3582_identity_matches(vendor, device1, device2)) {
        ESP_LOGW(TAG, "no chip at 0x%02x (ids %02x %02x %02x)", AMP_I2C_ADDRESS, vendor,
                 device1, device2);
        /* Off the bus entirely: a later call must not talk to a chip that is
         * not there, and repair() checks the handle. */
        (void)i2c_master_bus_rm_device(s_device);
        s_device = NULL;
        return probed != ESP_OK ? probed : ESP_ERR_NOT_FOUND;
    }
    uint8_t revision = 0U;
    (void)read_register(SSM3582_REG_REVISION, &revision);
    ESP_LOGI(TAG, "SSM3582 found at 0x%02x, revision %02x", AMP_I2C_ADDRESS, revision);

    const esp_err_t configured = configure();
    if (configured != ESP_OK) return configured;
    s_present = true;
    ESP_LOGI(TAG, "configured: I2S 16-bit stereo, muted, powered down");
    return ESP_OK;
}

bool board_ssm3582_present(void) { return s_present; }

/* Writes the mute bits to match what the gate and the knob together say - the
 * one place the two are combined, so a pause cannot end onto a channel the
 * knob turned down, and a turn up cannot open while paused. */
static esp_err_t apply_mute(void)
{
    const bool want = ssm3582_should_mute(s_gate_closed, s_volume);
    if (want == s_muted) return ESP_OK;
    if (want) {
        s_dac_ctrl |= SSM3582_DAC_CTRL_MUTE_LEFT | SSM3582_DAC_CTRL_MUTE_RIGHT;
    } else {
        s_dac_ctrl &= (uint8_t)~(SSM3582_DAC_CTRL_MUTE_LEFT | SSM3582_DAC_CTRL_MUTE_RIGHT);
    }
    ESP_RETURN_ON_ERROR(write_dac_ctrl(), TAG, "DAC mute");
    s_muted = want;
    ESP_LOGI(TAG, "amplifier %s", want ? "muted" : "unmuted");
    return ESP_OK;
}

static esp_err_t set_volume(uint8_t percent)
{
    if (percent > SSM3582_VOLUME_MAX_PERCENT) percent = SSM3582_VOLUME_MAX_PERCENT;
    /* The attenuation is written first, wherever it is heading, so a turn of
     * the knob that also unmutes lands on the right level rather than on the
     * chip's previous one. The mute bits then follow the gate and the knob. */
    ESP_RETURN_ON_ERROR(write_volumes(ssm3582_volume_register(percent)), TAG, "volume");
    s_volume = percent;
    ESP_LOGI(TAG, "amplifier volume %u%% (attenuation code 0x%02x)", (unsigned)percent,
             ssm3582_volume_register(percent));
    return apply_mute();
}

esp_err_t board_ssm3582_set_volume(uint8_t percent)
{
    LOCKED(set_volume(percent));
}

uint8_t board_ssm3582_volume(void) { return s_volume; }

static esp_err_t set_muted(bool muted)
{
    if (muted == s_gate_closed) return apply_mute();
    s_gate_closed = muted;
    return apply_mute();
}

esp_err_t board_ssm3582_set_muted(bool muted)
{
    LOCKED(set_muted(muted));
}

static esp_err_t set_powered(bool powered)
{
    if (powered == s_powered) return ESP_OK;
    s_power_ctrl = ssm3582_power_ctrl_word(powered);
    ESP_RETURN_ON_ERROR(write_power_ctrl(), TAG, "power control");
    s_powered = powered;
    if (powered) {
        /* Coming back up, the chip's DAC control may be at its reset default
         * rather than what was written before the power-down; rewriting the
         * tracked word restores mute state and rate family. The caller
         * unmutes when there is something to play. */
        ESP_RETURN_ON_ERROR(write_dac_ctrl(), TAG, "DAC control after power-up");
        ESP_RETURN_ON_ERROR(write_volumes(ssm3582_volume_register(s_volume)), TAG,
                            "volume after power-up");
    }
    ESP_LOGI(TAG, "amplifier %s", powered ? "powered up" : "powered down");
    return ESP_OK;
}

esp_err_t board_ssm3582_set_powered(bool powered)
{
    LOCKED(set_powered(powered));
}

static bool repair(void)
{
    const struct {
        uint8_t reg;
        uint8_t want;
    } setup[] = {
        {SSM3582_REG_DAC_CTRL, s_dac_ctrl},
        {SSM3582_REG_VOL_LEFT_CTRL, ssm3582_volume_register(s_volume)},
        {SSM3582_REG_VOL_RIGHT_CTRL, ssm3582_volume_register(s_volume)},
        {SSM3582_REG_POWER_CTRL, s_power_ctrl},
    };
    bool repaired = false;
    for (size_t i = 0U; i < sizeof(setup) / sizeof(setup[0]); ++i) {
        uint8_t value = 0U;
        if (read_register(setup[i].reg, &value) != ESP_OK) continue;
        if (value == setup[i].want) continue;
        ESP_LOGW(TAG, "%02xh was %02x, not %02x; writing it again", setup[i].reg, value,
                 setup[i].want);
        (void)write_register(setup[i].reg, setup[i].want);
        repaired = true;
    }
    return repaired;
}

bool board_ssm3582_repair(void)
{
    if (s_device == NULL || !s_present) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool repaired = repair();
    xSemaphoreGive(s_lock);
    return repaired;
}

static esp_err_t read_volume(uint8_t *left, uint8_t *right)
{
    ESP_RETURN_ON_ERROR(read_register(SSM3582_REG_VOL_LEFT_CTRL, left), TAG, "volume left");
    return read_register(SSM3582_REG_VOL_RIGHT_CTRL, right);
}

esp_err_t board_ssm3582_read_volume(uint8_t *left, uint8_t *right)
{
    ESP_RETURN_ON_FALSE(left != NULL && right != NULL, ESP_ERR_INVALID_ARG, TAG, "handles");
    LOCKED(read_volume(left, right));
}


