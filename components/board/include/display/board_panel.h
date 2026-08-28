#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_types.h"

/* The one thing that genuinely differs between display controllers: how a
 * panel handle is made from an SPI I/O handle, and what has to be sent before
 * the first pixel. Everything else board.c does with a display - the SPI bus,
 * the backlight PWM, the flush in bands, the MADCTL rotation - is written
 * against the esp_lcd panel interface and is the same for any part.
 *
 * So there is exactly one function per controller, in display/<part>.c, and
 * board.c does not include a vendor header at all. Adding a panel is a file
 * here plus a profile header, not a search through board.c for the places that
 * named the old one.
 *
 * The implementation for the part that board_options.h did not select compiles
 * to nothing, so an unselected vendor driver costs no flash. */
esp_err_t board_panel_create(esp_lcd_panel_io_handle_t io,
                             esp_lcd_panel_handle_t *out_panel);
