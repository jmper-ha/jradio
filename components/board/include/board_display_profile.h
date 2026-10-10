#pragma once

#include "board_options.h"

/* Everything that follows from the DISPLAY selection: panel geometry, the
 * clocks the controller and its backlight accept, and the orientation quirks
 * of the panel as mounted in this device. Kept out of board_options.h because
 * none of it is a wiring decision - swapping the panel for another part
 * changes all of it at once, which is exactly what selecting a different
 * DISPLAY does here.
 *
 * This file is only the dispatcher. One header per controller under display/
 * supplies the constants, so a new part is a new file next to the others
 * instead of another arm of a growing #if, and two controllers can be read
 * side by side. Everything downstream - board.c, ui.c, the splash generator -
 * includes this header and never a part-specific one. */

#if DISPLAY == DISPLAY_ILI9341_320_240 || DISPLAY == DISPLAY_ILI9341_240_320
#include "display/ili9341.h"
#elif DISPLAY == DISPLAY_ST7789_320_240 || DISPLAY == DISPLAY_ST7789_240_320 || \
    DISPLAY == DISPLAY_ST7789_320_170
#include "display/st7789.h"
#elif DISPLAY == DISPLAY_ILI9488_480_320 || DISPLAY == DISPLAY_ILI9488_320_480
#include "display/ili9488.h"
#elif DISPLAY == DISPLAY_ST7796S_480_320 || DISPLAY == DISPLAY_ST7796S_320_480
#include "display/st7796s.h"
#else
#error "unsupported DISPLAY - see board_parts.h for the parts with drivers"
#endif

/* Backlight PWM. Shared rather than per part: this drives the module's LED
 * string through the board's transistor, and neither the string nor the LEDC
 * peripheral cares which controller sits behind the glass.
 *
 * 5 kHz is well above anything the eye or the panel can follow, and low enough
 * that 13 bits of duty still fit the LEDC timer: the source clock has to divide
 * down to freq * 2^bits. */
#define BACKLIGHT_PWM_HZ 5000
/* Duty resolution in bits. The driver's ledc_timer_bit_t enumerators are the
 * bit counts themselves, which board.c verifies rather than assumes. */
#define BACKLIGHT_DUTY_BITS 13

/* What a pixel costs on the wire, and whether it has to be turned round first.
 *
 * Two of the three controllers here take RGB565 big-endian and want the two
 * bytes swapped out of a little-endian buffer; the ILI9488 takes only 18-bit
 * colour over SPI and its driver reads the buffer as native uint16_t to do the
 * conversion itself. Both facts belong to the part, so both are defaulted to
 * what the 16-bit panels need and overridden by the one that differs - a panel
 * that says nothing gets what this board has always done.
 *
 * board.c sizes the SPI transfer from the first and skips the swap on the
 * second; nothing else in the firmware knows either number exists. */
#ifndef TFT_PIXEL_WIRE_BYTES
#define TFT_PIXEL_WIRE_BYTES 2
#endif
#ifndef TFT_PIXEL_BYTE_SWAP
#define TFT_PIXEL_BYTE_SWAP 1
#endif

/* How many rows of the panel one transfer carries. A band is two buffers -
 * the board's DMA band and LVGL's - plus, on a converting driver, a third one
 * inside it, so this is a memory decision as much as a bus one, and the
 * memory it spends is the internal SRAM this firmware is short of.
 *
 * Ten, on every panel. The 320 px panels used twenty until 2026-10-09, and
 * that is what clicked: on the bench a PCM5102 clicked with the 320x240 build
 * - with the ST7789 module or with none - and played clean with the 480x320
 * build on the same wires, whose bands were already ten rows. Ten rows on the
 * small build, together with the gentler pin drive in board.c, took the
 * clicks away; either alone did not. Half the band is also 12.8 KB of
 * internal RAM back on those panels. */
#ifndef LCD_DRAW_LINES
#define LCD_DRAW_LINES 10
#endif

/* The drive of the panel's SPI lines (gpio_drive_cap_t, 0-3): the weakest
 * unless a profile asks for more - see board.c and display/ili9488.h. */
#ifndef DISPLAY_PIN_DRIVE
#define DISPLAY_PIN_DRIVE 0
#endif

/* Which way the controller's scroll register moves the picture. Every panel
 * in the catalogue can shift its whole frame along its gate axis in hardware
 * (VSCRDEF/VSCRSAD - "vertical" in the datasheet, which is the panel's own
 * long axis: screen x on a landscape build, y on a portrait one), and the
 * screensaver floats its clock on that, tear-free and at no bus cost. The
 * sign depends on how the memory is mirrored, which the datasheet does not
 * settle and the panel does; 1 here reverses it. The ST7796S landscape build
 * was checked on the glass; the others are derived and marked so. */
#ifndef TFT_SCROLL_REVERSED
#define TFT_SCROLL_REVERSED 0
#endif

/* Where the glass starts in the controller's memory. A controller is made
 * for the largest panel of its family and a smaller module shows a window of
 * that memory, so a picture written at (0,0) lands off the visible edge unless
 * every address is shifted by the margin. Zero for a module that uses all of
 * its controller; the 320x170 ST7789 is the one in the catalogue that does
 * not. Stated in the panel's own axes after the swap - the y gap is along the
 * short side of a landscape build. */
#ifndef TFT_X_GAP
#define TFT_X_GAP 0
#endif
#ifndef TFT_Y_GAP
#define TFT_Y_GAP 0
#endif

/* The panel's reset line, when the module brings one out. Revision 1's ILI9341
 * has its reset tied to the ESP32's own, so no GPIO is wired and -1 is what
 * esp_lcd is told. The line itself belongs in board_options.h with the other
 * pins; this is only the default for the boards that have none. */
#ifndef TFT_RESET_GPIO
#define TFT_RESET_GPIO -1
#endif

/* The relation every profile has to satisfy, checked here so it is checked
 * once rather than once per part. A profile that swapped the geometry and
 * forgot which way up that leaves the screens would otherwise compile and only
 * show itself as a layout drawn off the edge. */
_Static_assert(BOARD_DISPLAY_PORTRAIT == (TFT_HEIGHT > TFT_WIDTH),
               "the profile's BOARD_DISPLAY_PORTRAIT disagrees with its own "
               "TFT_WIDTH and TFT_HEIGHT");
