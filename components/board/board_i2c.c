#include "board_i2c.h"

#include "board_config.h"
#include "esp_check.h"

static const char *TAG = "board_i2c";

static i2c_master_bus_handle_t s_bus;

esp_err_t board_i2c0_bus(i2c_master_bus_handle_t *bus)
{
    ESP_RETURN_ON_FALSE(bus != NULL, ESP_ERR_INVALID_ARG, TAG, "no handle");
    if (s_bus == NULL) {
        const board_config_t *board = board_config_get();
        ESP_RETURN_ON_FALSE(board->i2c0_sda >= 0 && board->i2c0_scl >= 0, ESP_ERR_INVALID_STATE,
                            TAG, "no I2C0 pins in the wiring");
        const i2c_master_bus_config_t config = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = board->i2c0_sda,
            .scl_io_num = board->i2c0_scl,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            /* On as well as the board's own: they cost nothing beside 4.7 k,
             * and a bench part without any still answers at 100 kHz. */
            .flags.enable_internal_pullup = true,
        };
        ESP_RETURN_ON_ERROR(i2c_new_master_bus(&config, &s_bus), TAG, "bus");
    }
    *bus = s_bus;
    return ESP_OK;
}
