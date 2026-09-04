#include "board_display_profile.h"
#include "board_options.h"

#if DISPLAY == DISPLAY_ILI9488_480_320 || DISPLAY == DISPLAY_ILI9488_320_480

#include "esp_check.h"
#include "esp_lcd_ili9488.h"
#include "esp_lcd_panel_ops.h"

#include "display/board_panel.h"

static const char *TAG = "board.ili9488";

/* Reset and init are the vendor driver's own sequence. Two arguments here are
 * not the shape the other panels' create functions have, and both are forced
 * by the same fact - this controller takes only 18-bit colour over SPI:
 *
 * - bits_per_pixel is 18, and passing 16 is not a smaller picture but a driver
 *   that sends two bytes where the panel expects three, which puts noise on
 *   the glass rather than an error in the log;
 * - the driver converts RGB565 to RGB666 into a buffer of its own, and that
 *   buffer's size is ours to choose. It has to hold a whole band, because a
 *   band is what board.c hands over in one call, and the driver takes it from
 *   MALLOC_CAP_DMA - internal SRAM - at three bytes a pixel. LCD_DRAW_LINES is
 *   set to ten in this panel's profile for exactly that reason.
 *
 * The rotation is not set here on purpose - MADCTL is the same register on
 * every controller in the catalogue, so board.c applies TFT_SWAP_XY and the
 * mirror baseline once for all of them. Worth knowing while reading that code
 * against a MADCTL dump: this driver inverts the sense of mirror_x, clearing
 * the MX bit for true where the ILI9341's sets it. It cancels out, because the
 * baseline in this panel's profile was measured through the same call. */
esp_err_t board_panel_create(esp_lcd_panel_io_handle_t io,
                             esp_lcd_panel_handle_t *out_panel)
{
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = TFT_RESET_GPIO,
        .rgb_ele_order = TFT_RGB_ORDER_BGR ? LCD_RGB_ELEMENT_ORDER_BGR :
                                             LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 18,
    };
    esp_lcd_panel_handle_t panel = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9488(io, &panel_config,
                                                  TFT_WIDTH * LCD_DRAW_LINES,
                                                  &panel), TAG,
                        "create ILI9488 panel failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "reset ILI9488 failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "initialize ILI9488 failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(panel, TFT_INVERT_COLOR), TAG,
                        "set ILI9488 inversion failed");
    *out_panel = panel;
    return ESP_OK;
}

#else

/* Not the selected panel. Keeping one declaration means the translation unit
 * is still valid C rather than empty. */
typedef int board_panel_ili9488_unselected_t;

#endif
