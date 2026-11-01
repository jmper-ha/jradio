#include "board_touch.h"

#include "board_config.h"
#include "board_display_profile.h"
#include "board_i2c.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "touch_chip.h"

static const char *TAG = "touch";

#define TOUCH_I2C_HZ 100000U
#define TOUCH_I2C_TIMEOUT_MS 50
#define TOUCH_I2C_TRIES 3

static i2c_master_dev_handle_t s_device;
static int s_int_gpio = BOARD_PIN_NONE;
static volatile bool s_mirror_x;
static volatile bool s_mirror_y;
static bool s_down;
static board_touch_point_t s_point;

void board_touch_set_mirror(bool mirror_x, bool mirror_y)
{
    s_mirror_x = mirror_x;
    s_mirror_y = mirror_y;
}

esp_err_t board_touch_init(void)
{
    const board_config_t *board = board_config_get();
    i2c_master_bus_handle_t bus = NULL;
    ESP_RETURN_ON_ERROR(board_i2c0_bus(&bus), TAG, "bus");
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TOUCH_CHIP_I2C_ADDRESS,
        .scl_speed_hz = TOUCH_I2C_HZ,
    };
    i2c_master_dev_handle_t device = NULL;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &config, &device), TAG, "device");

    const uint8_t id_reg = TOUCH_CHIP_REG_ID;
    uint8_t id = 0U;
    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < TOUCH_I2C_TRIES && err != ESP_OK; ++attempt) {
        err = i2c_master_transmit_receive(device, &id_reg, 1, &id, 1, TOUCH_I2C_TIMEOUT_MS);
    }
    if (err == ESP_OK && id != TOUCH_CHIP_ID_FT6336U) err = ESP_ERR_NOT_FOUND;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "no FT6336U on SDA %d / SCL %d (chip id 0x%02x): %s", board->i2c0_sda,
                 board->i2c0_scl, id, esp_err_to_name(err));
        (void)i2c_master_bus_rm_device(device);
        return err;
    }

    /* Without the level mode the line only pulses, and read as a level it
     * would say nobody is touching: fall back to asking the chip every time. */
    const uint8_t mode[2] = {TOUCH_CHIP_REG_MODE, TOUCH_CHIP_MODE_LEVEL};
    err = ESP_FAIL;
    for (int attempt = 0; attempt < TOUCH_I2C_TRIES && err != ESP_OK; ++attempt) {
        err = i2c_master_transmit(device, mode, sizeof(mode), TOUCH_I2C_TIMEOUT_MS);
    }
    if (board->touch_int >= 0 && err == ESP_OK) {
        const gpio_config_t input = {
            .pin_bit_mask = 1ULL << board->touch_int,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&input), TAG, "interrupt pin");
        s_int_gpio = board->touch_int;
    }
    s_device = device;
    ESP_LOGI(TAG, "FT6336U found on SDA %d / SCL %d, %s", board->i2c0_sda, board->i2c0_scl,
             s_int_gpio >= 0 ? "interrupt line in use" : "polled");
    return ESP_OK;
}

bool board_touch_read(board_touch_point_t *point)
{
    if (s_device == NULL) return false;
    if (s_int_gpio >= 0 && gpio_get_level(s_int_gpio) != 0) {
        s_down = false;
        return false;
    }
    const uint8_t first = TOUCH_CHIP_REG_REPORT;
    uint8_t regs[TOUCH_CHIP_REPORT_REGS];
    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < TOUCH_I2C_TRIES && err != ESP_OK; ++attempt) {
        err = i2c_master_transmit_receive(s_device, &first, 1, regs, sizeof(regs),
                                          TOUCH_I2C_TIMEOUT_MS);
    }
    if (err == ESP_OK) {
        touch_chip_point_t chip;
        s_down = touch_chip_decode(regs, &chip);
        if (s_down) {
            const touch_chip_point_t screen = touch_chip_place(
                chip, TFT_WIDTH, TFT_HEIGHT, TFT_SWAP_XY, s_mirror_x, s_mirror_y);
            s_point = (board_touch_point_t){screen.x, screen.y};
        }
    }
    if (s_down) *point = s_point;
    return s_down;
}
