#include "board_display_profile.h"
#include "board_options.h"

#if DISPLAY == DISPLAY_ILI9341_320_240 || DISPLAY == DISPLAY_ILI9341_240_320

#include "esp_check.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_ops.h"

#include "display/board_panel.h"

static const char *TAG = "board.ili9341";

/* Reset and init are the vendor driver's own sequence; this panel needs
 * nothing sent after it. The rotation is not set here on purpose - MADCTL is
 * the same register on every controller in the catalogue, so board.c applies
 * TFT_SWAP_XY and the mirror baseline once for all of them. */
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
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(io, &panel_config, &panel), TAG,
                        "create ILI9341 panel failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "reset ILI9341 failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "initialize ILI9341 failed");
    *out_panel = panel;
    return ESP_OK;
}

#else

/* Not the selected panel. Keeping one declaration means the translation unit
 * is still valid C rather than empty. */
typedef int board_panel_ili9341_unselected_t;

#endif
