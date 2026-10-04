#include "board_display_profile.h"
#include "board_options.h"

#if DISPLAY == DISPLAY_ILI9341_320_240 || DISPLAY == DISPLAY_ILI9341_240_320

#include "esp_check.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_ops.h"

#include "display/board_panel.h"

static const char *TAG = "board.ili9341";

/* Adafruit's and TFT_eSPI's VCOM and gamma, sent over the vendor driver's.
 * Espressif's tables left the grey lines washed out on the modules people
 * actually buy - which are sold against these - and side by side on the
 * bench (2026-10-04) these were the clearly better of five; VCOM is part of
 * it, since it sets the contrast as much as the tables do. */
static const struct {
    uint8_t command;
    uint8_t length;
    uint8_t data[15];
} k_contrast[] = {
    {0xC5, 2, {0x3E, 0x28}},
    {0xC7, 1, {0x86}},
    {0x26, 1, {0x01}},
    {0xE0, 15, {0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E,
                0x09, 0x00}},
    {0xE1, 15, {0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31,
                0x36, 0x0F}},
};

/* Reset and init are the vendor driver's own sequence, with the contrast
 * above sent after it. The rotation is not set here on purpose - MADCTL is
 * the same register on every controller in the catalogue, so board.c applies
 * TFT_SWAP_XY and the mirror baseline once for all of them. */
esp_err_t board_panel_create(esp_lcd_panel_io_handle_t io,
                             esp_lcd_panel_handle_t *out_panel)
{
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = board_panel_reset_gpio(),
        .rgb_ele_order = TFT_RGB_ORDER_BGR ? LCD_RGB_ELEMENT_ORDER_BGR :
                                             LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    esp_lcd_panel_handle_t panel = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(io, &panel_config, &panel), TAG,
                        "create ILI9341 panel failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "reset ILI9341 failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "initialize ILI9341 failed");
    for (size_t index = 0; index < sizeof(k_contrast) / sizeof(k_contrast[0]); ++index) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, k_contrast[index].command,
                                                      k_contrast[index].data,
                                                      k_contrast[index].length),
                            TAG, "set ILI9341 gamma failed");
    }
    *out_panel = panel;
    return ESP_OK;
}

#else

/* Not the selected panel. Keeping one declaration means the translation unit
 * is still valid C rather than empty. */
typedef int board_panel_ili9341_unselected_t;

#endif
