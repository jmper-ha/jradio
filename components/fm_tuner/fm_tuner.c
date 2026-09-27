#include "fm_tuner.h"

#include "board_config.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "fm_tuner";

/* 100 kHz rather than the chip's 400: the tuner sits on wires across a bench
 * with 4.7 k pull-ups, and a register write a few times a second has no use
 * for the speed. */
#define FM_I2C_HZ 100000U
#define FM_I2C_TIMEOUT_MS 50
/* After the soft reset, before the chip takes a tune: the crystal starting
 * up. Too short and the first tune is lost without an error. */
#define FM_POWER_UP_MS 100

/* The bus is created here because the tuner is its only device so far. The
 * PCM5122 will share it, and then it moves to the board with a getter. */
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_device;
static SemaphoreHandle_t s_lock;
static rda5807_state_t s_state = {.volume = RDA5807_VOLUME_MAX};

/* Held for a whole call, a transfer or a few: the power-up is a sequence the
 * status read must not land in the middle of. */
#define LOCKED(call)                                                              \
    do {                                                                          \
        ESP_RETURN_ON_FALSE(s_device != NULL, ESP_ERR_INVALID_STATE, TAG, "no tuner"); \
        xSemaphoreTake(s_lock, portMAX_DELAY);                                    \
        const esp_err_t locked_result = (call);                                   \
        xSemaphoreGive(s_lock);                                                   \
        return locked_result;                                                     \
    } while (0)

static esp_err_t write_register(uint8_t reg, uint16_t value)
{
    const uint8_t bytes[3] = {reg, (uint8_t)(value >> 8), (uint8_t)value};
    return i2c_master_transmit(s_device, bytes, sizeof(bytes), FM_I2C_TIMEOUT_MS);
}

static esp_err_t read_register(uint8_t reg, uint16_t *value)
{
    uint8_t bytes[2] = {0};
    ESP_RETURN_ON_ERROR(
        i2c_master_transmit_receive(s_device, &reg, 1, bytes, sizeof(bytes), FM_I2C_TIMEOUT_MS),
        TAG, "read %02x", reg);
    *value = (uint16_t)((bytes[0] << 8) | bytes[1]);
    return ESP_OK;
}

esp_err_t fm_tuner_init(void)
{
    const board_config_t *board = board_config_get();
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock != NULL, ESP_ERR_NO_MEM, TAG, "lock");
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = board->i2c0_sda,
        .scl_io_num = board->i2c0_scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        /* On as well as the board's own: they cost nothing beside 4.7 k, and
         * a bench tuner without any still answers at this speed. */
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &s_bus), TAG, "bus");
    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = RDA5807_I2C_ADDRESS,
        .scl_speed_hz = FM_I2C_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(s_bus, &device_config, &s_device);
    uint16_t chip_id = 0U;
    if (err == ESP_OK) err = read_register(RDA5807_REG_CHIP_ID, &chip_id);
    if (err == ESP_OK && (chip_id >> 8) != RDA5807_CHIP_ID_FAMILY) err = ESP_ERR_NOT_FOUND;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "no RDA5807 on SDA %d / SCL %d (chip id 0x%04x): %s", board->i2c0_sda,
                 board->i2c0_scl, chip_id, esp_err_to_name(err));
        if (s_device != NULL) (void)i2c_master_bus_rm_device(s_device);
        (void)i2c_del_master_bus(s_bus);
        s_device = NULL;
        s_bus = NULL;
        return err;
    }
    ESP_LOGI(TAG, "RDA5807 found on SDA %d / SCL %d, chip id 0x%04x", board->i2c0_sda,
             board->i2c0_scl, chip_id);
    return ESP_OK;
}

bool fm_tuner_present(void)
{
    return s_device != NULL;
}

static esp_err_t power(bool on)
{
    s_state.enabled = on;
    if (!on) return write_register(RDA5807_REG_CONTROL, rda5807_control_word(&s_state, false, false, false));
    ESP_RETURN_ON_ERROR(write_register(RDA5807_REG_CONTROL,
                                       rda5807_control_word(&s_state, true, false, false)),
                        TAG, "reset");
    ESP_RETURN_ON_ERROR(write_register(RDA5807_REG_CONTROL,
                                       rda5807_control_word(&s_state, false, false, false)),
                        TAG, "enable");
    vTaskDelay(pdMS_TO_TICKS(FM_POWER_UP_MS));
    ESP_RETURN_ON_ERROR(write_register(RDA5807_REG_OPTIONS, rda5807_options_word()), TAG, "options");
    return write_register(RDA5807_REG_VOLUME, rda5807_volume_word(s_state.volume));
}

static esp_err_t set_volume(uint8_t volume)
{
    s_state.volume = volume > RDA5807_VOLUME_MAX ? RDA5807_VOLUME_MAX : volume;
    return write_register(RDA5807_REG_VOLUME, rda5807_volume_word(s_state.volume));
}

static esp_err_t set_muted(bool muted)
{
    s_state.muted = muted;
    return write_register(RDA5807_REG_CONTROL, rda5807_control_word(&s_state, false, false, false));
}

static esp_err_t read_status(rda5807_status_t *status)
{
    uint16_t words[2] = {0U, 0U};
    ESP_RETURN_ON_ERROR(read_register(RDA5807_REG_STATUS, &words[0]), TAG, "status");
    ESP_RETURN_ON_ERROR(read_register(RDA5807_REG_SIGNAL, &words[1]), TAG, "signal");
    rda5807_parse_status(words[0], words[1], status);
    return ESP_OK;
}

static esp_err_t read_rds(uint16_t blocks[4], bool *ready, bool *block_a_ok, bool *block_b_ok)
{
    rda5807_status_t status;
    *ready = false;
    ESP_RETURN_ON_ERROR(read_status(&status), TAG, "rds status");
    if (!status.rds_ready) return ESP_OK;
    for (uint8_t i = 0U; i < 4U; ++i) {
        ESP_RETURN_ON_ERROR(read_register((uint8_t)(RDA5807_REG_RDS_A + i), &blocks[i]), TAG,
                            "rds block");
    }
    *ready = true;
    // One or two bits corrected is still the block it says it is.
    *block_a_ok = status.rds_block_a_errors <= 1U;
    *block_b_ok = status.rds_block_b_errors <= 1U;
    return ESP_OK;
}

esp_err_t fm_tuner_power(bool on)
{
    LOCKED(power(on));
}

esp_err_t fm_tuner_tune(uint32_t khz)
{
    LOCKED(write_register(RDA5807_REG_CHANNEL, rda5807_tune_word(khz)));
}

esp_err_t fm_tuner_seek(bool up)
{
    LOCKED(write_register(RDA5807_REG_CONTROL, rda5807_control_word(&s_state, false, true, up)));
}

esp_err_t fm_tuner_set_volume(uint8_t volume)
{
    LOCKED(set_volume(volume));
}

esp_err_t fm_tuner_set_muted(bool muted)
{
    LOCKED(set_muted(muted));
}

esp_err_t fm_tuner_status(rda5807_status_t *status)
{
    ESP_RETURN_ON_FALSE(status != NULL, ESP_ERR_INVALID_ARG, TAG, "status");
    LOCKED(read_status(status));
}

esp_err_t fm_tuner_read_rds(uint16_t blocks[4], bool *ready, bool *block_a_ok, bool *block_b_ok)
{
    ESP_RETURN_ON_FALSE(blocks != NULL && ready != NULL && block_a_ok != NULL && block_b_ok != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "rds");
    LOCKED(read_rds(blocks, ready, block_a_ok, block_b_ok));
}
