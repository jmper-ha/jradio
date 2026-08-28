#include "board_display_profile.h"
#include "board_options.h"

#if DISPLAY == DISPLAY_ST7789_320_240 || DISPLAY == DISPLAY_ST7789_240_320

#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"

#include "display/board_panel.h"

static const char *TAG = "board.st7789";

/* No managed component to pull in: unlike the ILI9341, this controller's
 * driver ships inside ESP-IDF's own esp_lcd. */
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
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(io, &panel_config, &panel), TAG,
                        "create ST7789 panel failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "reset ST7789 failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "initialize ST7789 failed");
    /* The whole reason this controller needs code of its own: its init leaves
     * the panel inverted and esp_lcd's sequence does not send INVON, so
     * without this every colour arrives as its complement. */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(panel, TFT_INVERT_COLOR != 0), TAG,
                        "set ST7789 colour inversion failed");
    *out_panel = panel;
    return ESP_OK;
}

#else

/* Not the selected panel. Keeping one declaration means the translation unit
 * is still valid C rather than empty. */
typedef int board_panel_st7789_unselected_t;

#endif
