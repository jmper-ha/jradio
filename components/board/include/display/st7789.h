#pragma once

#include "board_options.h"

/* Everything that follows from selecting one of the ST7789 panels. The shape
 * of this header is deliberately the same as display/ili9341.h: the two
 * controllers differ in a handful of constants and one extra init command, and
 * putting them side by side is what keeps that visible.
 *
 * The landscape part was brought up on a panel on 2026-08-28 and every value it
 * uses is a measurement. Portrait has not been built yet and says so where it
 * guesses. */

/* Measured 2026-08-28: colours came out right with RGB, so this module is
 * wired the other way round from the BGR ILI9341 the device ships with. Wrong
 * here swaps red and blue, which the boot splash shows at once. */
#define TFT_RGB_ORDER_BGR 0

/* Measured 2026-08-28, and the reverse of what these modules are usually
 * described as needing: this one displays what it is sent, so the INVON that
 * the common advice calls for turned the whole splash into a negative. Sent as
 * INVOFF rather than skipped, so the state is the firmware's and not whatever
 * the controller happened to reset into. */
#define TFT_INVERT_COLOR 0

/* Geometry and the MADCTL baseline. Like the ILI9341, this controller is
 * natively portrait and landscape is the same glass with MADCTL MV set, so the
 * two part numbers differ in three numbers and nothing else. */
#if DISPLAY == DISPLAY_ST7789_240_320

#define TFT_WIDTH 240
#define TFT_HEIGHT 320
#define TFT_SWAP_XY 0
/* Derived, not measured - no portrait ST7789 has been built.
 * The relation that is worth carrying across is the one measured within a
 * single panel: on the ILI9341, portrait needed MX flipped against landscape,
 * because clearing MV transposes rather than rotates and MX is what undoes the
 * mirror that leaves behind. Applying that to this panel's measured landscape
 * pair below gives what stands here. Nothing about the ILI9341's own values
 * carries over - the two vendor init sequences leave different MADCTL
 * defaults, so the panels are only comparable to themselves. If a portrait
 * build comes up mirrored, this is the line that was a guess. */
#define TFT_MIRROR_X 0
#define TFT_MIRROR_Y 0

#else

#define TFT_WIDTH 320
#define TFT_HEIGHT 240
#define TFT_SWAP_XY 1
/* Measured 2026-08-28, one mirror at a time on the panel: the image needed
 * flipping horizontally and nothing else. Worth recording that a half turn was
 * tried first and was wrong by exactly the vertical mirror, because the
 * ILI9341 in the same enclosure needs neither bit - the difference is not how
 * the glass is mounted but the MADCTL the two vendor init sequences leave
 * behind, which is why neither panel's baseline can be reasoned out from the
 * other's. */
#define TFT_MIRROR_X 1
#define TFT_MIRROR_Y 0

#endif

/* The plain 0/1 the layout code tests, so no screen has to know which part
 * numbers exist to ask which way up this one is. */
#if DISPLAY == DISPLAY_ST7789_240_320
#define BOARD_DISPLAY_PORTRAIT 1
#else
#define BOARD_DISPLAY_PORTRAIT 0
#endif

/* The controller is good for far more than this - the datasheet's write cycle
 * allows tens of MHz - but the clock a panel actually takes is decided by the
 * ribbon and the wiring, and both are this board's. Kept at the rate the
 * ILI9341 was measured good at, which brought this panel up clean on the first
 * try. Raising it is worth doing with the panel in front of you: the failure
 * is torn or speckled pixels, not a dead screen. */
#define DISPLAY_PIXEL_CLOCK_HZ (20 * 1000 * 1000)

/* Named for the log line and the error messages, so a boot log says which
 * panel the firmware was built for without anyone reading board_options.h. */
#define BOARD_PANEL_NAME "ST7789"
