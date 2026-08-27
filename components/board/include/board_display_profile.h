#pragma once

#include "board_options.h"

/* Everything that follows from the DISPLAY selection: panel geometry, the
 * clocks the controller and its backlight accept, and the orientation quirks
 * of the panel as mounted in this device. Kept out of board_options.h because
 * none of it is a wiring decision - swapping the panel for another part
 * changes all of it at once, which is exactly what selecting a different
 * DISPLAY does here. */

#if DISPLAY == DISPLAY_ILI9341_320

#define TFT_WIDTH 320
#define TFT_HEIGHT 240
/* Verified on the connected TFT: blue data is rendered red with MADCTL RGB. */
#define TFT_RGB_ORDER_BGR 1
/* The panel is mounted landscape, then rotated 180 degrees. This is the
 * baseline the device boots with; the user's two flip settings compose with it
 * by XOR in board_display_set_rotation(), so a non-zero value here survives a
 * switch being touched and can still be turned off by one. */
#define TFT_SWAP_XY 1
#define TFT_MIRROR_X 0
#define TFT_MIRROR_Y 0

/* SPI clock for the panel - a property of the controller and the ribbon it
 * comes on, not of this board. Probing higher needs a scope on the bus, not a
 * guess. */
#define DISPLAY_PIXEL_CLOCK_HZ (20 * 1000 * 1000)

/* Backlight PWM. 5 kHz is well above anything the eye or the panel can follow,
 * and low enough that 13 bits of duty still fit the LEDC timer: the source
 * clock has to divide down to freq * 2^bits. */
#define BACKLIGHT_PWM_HZ 5000
/* Duty resolution in bits. The driver's ledc_timer_bit_t enumerators are the
 * bit counts themselves, which board.c verifies rather than assumes. */
#define BACKLIGHT_DUTY_BITS 13

#else
#error "unsupported DISPLAY - see board_parts.h for the parts with drivers"
#endif
