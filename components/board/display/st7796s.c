#include "board_display_profile.h"
#include "board_options.h"

#if DISPLAY == DISPLAY_ST7796S_480_320 || DISPLAY == DISPLAY_ST7796S_320_480

#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7796.h"

#include "display/board_panel.h"

static const char *TAG = "board.st7796s";

/* Sixteen bits per pixel, which is the whole reason this file exists next to
 * display/ili9488.c: the same glass, the same module, three bytes a pixel there
 * and two here. See display/st7796s.h for what that buys.
 *
 * The vendor sequence is the driver's own default. It opens the extended
 * command set with 0xF0, writes Sitronix's gamma and power tables, and locks it
 * again - the part of an init sequence that a foreign driver cannot supply and
 * that the panel had been running without.
 *
 * The rotation is not set here on purpose - MADCTL is the same register on
 * every controller in the catalogue, so board.c applies TFT_SWAP_XY and the
 * mirror baseline once for all of them. Unlike the ILI9488's driver, this one
 * means MX when it says mirror_x, which is why the two profiles' baselines
 * differ by more than the panel does. */
esp_err_t board_panel_create(esp_lcd_panel_io_handle_t io,
                             esp_lcd_panel_handle_t *out_panel)
{
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = TFT_RESET_GPIO,
        .rgb_ele_order = TFT_RGB_ORDER_BGR ? LCD_RGB_ELEMENT_ORDER_BGR :
                                             LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    esp_lcd_panel_handle_t panel = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7796(io, &panel_config, &panel), TAG,
                        "create ST7796S panel failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "reset ST7796S failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "initialize ST7796S failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(panel, TFT_INVERT_COLOR != 0), TAG,
                        "set ST7796S inversion failed");
    *out_panel = panel;
    return ESP_OK;
}

#else

/* Not the selected panel. Keeping one declaration means the translation unit
 * is still valid C rather than empty. */
typedef int board_panel_st7796s_unselected_t;

#endif
