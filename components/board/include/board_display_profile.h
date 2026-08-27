#pragma once

#include "board_options.h"

/* Everything that follows from the DISPLAY selection: panel geometry, the
 * clocks the controller and its backlight accept, and the orientation quirks
 * of the panel as mounted in this device. Kept out of board_options.h because
 * none of it is a wiring decision - swapping the panel for another part
 * changes all of it at once, which is exactly what selecting a different
 * DISPLAY does here. */

#if DISPLAY == DISPLAY_ILI9341_320_240 || DISPLAY == DISPLAY_ILI9341_240_320

/* Verified on the connected TFT: blue data is rendered red with MADCTL RGB. */
#define TFT_RGB_ORDER_BGR 1

/* Geometry and the MADCTL baseline, both decided by which of the two panels
 * board_options.h selected.
 *
 * The panel is natively portrait - 240 wide by 320 tall is how the controller
 * addresses it - and landscape is not a different module but the same one with
 * MADCTL MV set, which is what swaps the axes. So the two differ in exactly
 * three numbers, and everything downstream is written against TFT_WIDTH and
 * TFT_HEIGHT rather than against 320 and 240.
 *
 * TFT_MIRROR_X / TFT_MIRROR_Y are the baseline the device boots with. The
 * user's two flip settings compose with it by XOR in
 * board_display_set_rotation(), so a non-zero value here survives a switch
 * being touched and can still be turned off by one. */
#if DISPLAY == DISPLAY_ILI9341_240_320

#define TFT_WIDTH 240
#define TFT_HEIGHT 320
#define TFT_SWAP_XY 0
/* Checked on the panel 2026-08-27. Clearing MV alone transposes the image
 * rather than rotating it, and a transpose is a rotation plus a mirror - the
 * first portrait build came up mirrored left to right, which is that mirror.
 * MX undoes it: with MV clear the controller's columns are the screen's X
 * axis, so this is the bit that acts horizontally here. */
#define TFT_MIRROR_X 1
#define TFT_MIRROR_Y 0

#else

#define TFT_WIDTH 320
#define TFT_HEIGHT 240
#define TFT_SWAP_XY 1
/* Verified on the panel 2026-08-16. */
#define TFT_MIRROR_X 0
#define TFT_MIRROR_Y 0

#endif

/* The plain 0/1 the layout code tests, so no screen has to know which part
 * numbers exist to ask which way up this one is. */
#if DISPLAY == DISPLAY_ILI9341_240_320
#define BOARD_DISPLAY_PORTRAIT 1
#else
#define BOARD_DISPLAY_PORTRAIT 0
#endif

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
