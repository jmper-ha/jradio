#pragma once

#include "board_options.h"

/* Everything that follows from selecting the NV3007 bar panel: 142x428 glass
 * on a controller whose memory is 168 columns wide, mounted lying down.
 *
 * The module came from a seller with no description, so every number below
 * that is not the glass's size is a first guess, to be put right on the panel
 * the way the 320x170 module's were. Each says what it was guessed from. */

/* RGB: the splash's green eye and red wordmark came up as drawn, 2026-10-26. */
#define TFT_RGB_ORDER_BGR 0

/* INVOFF, as ArduinoGFX drives this module; seen right on the panel. */
#define TFT_INVERT_COLOR 0

#define TFT_WIDTH 428
#define TFT_HEIGHT 142
#define TFT_SWAP_XY 1
/* The 142 columns shown are not the middle of the 168: ArduinoGFX's numbers
 * for this module are 12 before and 14 after. After the swap the columns are
 * the screen's rows, so the gap is in y, as on the 320x170 module. Which of
 * the two it is depends on the mirror below: if the picture comes up two
 * rows off with a line of noise on one edge, the other one is 14. Seen clean
 * on the panel at 12 with the mirror pair below. */
#define TFT_X_GAP 0
#define TFT_Y_GAP 12
/* Seen on the panel 2026-10-26: the first guess, MX 1 / MY 0, came up half a
 * turn round - the user righted it with both flips in the settings - so the
 * pair is that guess with both bits turned over. */
#define TFT_MIRROR_X 0
#define TFT_MIRROR_Y 1

#define BOARD_DISPLAY_PORTRAIT 0

/* The hardware scroll runs the other way on this glass, as on the ST7796S:
 * with the default sign the clock screensaver's block left by the near edge
 * and came back in at the far one. Seen on the panel 2026-10-27. */
#define TFT_SCROLL_REVERSED 1

/* 20 MHz. It was raised to 40, what the other panels on this wiring run
 * at, and looked clean for minutes - then twice in an evening, 2026-10-27,
 * the glass froze on its last frame while the firmware went on drawing and
 * answering the buttons: the controller had stopped taking the writes. Back
 * at 20 to see whether that was the clock. */
#define DISPLAY_PIXEL_CLOCK_HZ (20 * 1000 * 1000)

#define BOARD_PANEL_NAME "NV3007"
