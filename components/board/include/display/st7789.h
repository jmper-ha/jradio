#pragma once

#include "board_options.h"

/* Everything that follows from selecting one of the ST7789 panels. The shape
 * of this header is deliberately the same as display/ili9341.h: the two
 * controllers differ in a handful of constants and one extra init command, and
 * putting them side by side is what keeps that visible.
 *
 * NOT YET SEEN ON A PANEL. Every value below is what the datasheet and the
 * common wiring of these modules say; the ones marked so can only be settled
 * by looking at the screen, exactly as the ILI9341's were. */

/* Most ST7789 breakouts are wired RGB, unlike the BGR panel this device ships
 * with. Wrong here means blue and red swap - obvious on the first boot splash. */
#define TFT_RGB_ORDER_BGR 0

/* The one real behavioural difference from the ILI9341: an ST7789 comes up
 * displaying the inverse of what it is sent, and the ESP-IDF driver's init
 * sequence does not send INVON. Without this the splash arrives as a negative. */
#define TFT_INVERT_COLOR 1

/* Geometry and the MADCTL baseline. Like the ILI9341, this controller is
 * natively portrait and landscape is the same glass with MADCTL MV set, so the
 * two part numbers differ in three numbers and nothing else. */
#if DISPLAY == DISPLAY_ST7789_240_320

#define TFT_WIDTH 240
#define TFT_HEIGHT 320
#define TFT_SWAP_XY 0
/* Copied from the ILI9341 profile, where clearing MV was measured to transpose
 * rather than rotate and MX was the bit that undid it. The MADCTL bits mean the
 * same thing on this controller, but which way the glass is mounted in the
 * module does not, so check the first portrait build for a left-right mirror. */
#define TFT_MIRROR_X 1
#define TFT_MIRROR_Y 0

#else

#define TFT_WIDTH 320
#define TFT_HEIGHT 240
#define TFT_SWAP_XY 1
#define TFT_MIRROR_X 0
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
 * ribbon and the wiring, and both are this board's. Starting at the rate the
 * ILI9341 was measured good at costs one full-screen redraw per second and
 * removes a variable from the first bring-up. Raise it with the panel in front
 * of you, not from here. */
#define DISPLAY_PIXEL_CLOCK_HZ (20 * 1000 * 1000)

/* Named for the log line and the error messages, so a boot log says which
 * panel the firmware was built for without anyone reading board_options.h. */
#define BOARD_PANEL_NAME "ST7789"
