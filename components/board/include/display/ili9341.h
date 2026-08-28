#pragma once

#include "board_options.h"

/* Everything that follows from selecting one of the ILI9341 panels: geometry,
 * the clock the controller and its ribbon accept, and the orientation quirks
 * of the panel as mounted in this device. Included by board_display_profile.h
 * for the DISPLAY values named here and by nothing else - a screen asks that
 * header for TFT_WIDTH and never learns which controller answered. */

/* Verified on the connected TFT: blue data is rendered red with MADCTL RGB. */
#define TFT_RGB_ORDER_BGR 1

/* This panel's idle state is already the right way round; nothing sends INVON.
 * Named anyway rather than left implicit: inversion is a per-panel fact that
 * has to be looked at on a screen, and a macro every profile answers is easier
 * to spot than a call one of them forgot to make. */
#define TFT_INVERT_COLOR 0

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

/* Named for the log line and the error messages, so a boot log says which
 * panel the firmware was built for without anyone reading board_options.h. */
#define BOARD_PANEL_NAME "ILI9341"
