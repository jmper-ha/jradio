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
#elif DISPLAY == DISPLAY_ST7789_320_240 || DISPLAY == DISPLAY_ST7789_240_320
#include "display/st7789.h"
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
