#pragma once

#include "board_options.h"

/* Everything that follows from the DISPLAY selection: panel geometry and the
 * orientation quirks of the panel as mounted in this device. Kept out of
 * board_options.h because none of it is a wiring decision - swapping the panel
 * for another part changes all of it at once, which is exactly what selecting
 * a different DISPLAY does here. */

#if DISPLAY == DISPLAY_ILI9341_320

#define TFT_WIDTH 320
#define TFT_HEIGHT 240
/* Verified on the connected TFT: blue data is rendered red with MADCTL RGB. */
#define TFT_RGB_ORDER_BGR 1
/* The panel is mounted landscape, then rotated 180 degrees. */
#define TFT_SWAP_XY 1
#define TFT_MIRROR_X 0
#define TFT_MIRROR_Y 0

#else
#error "unsupported DISPLAY - see board_parts.h for the parts with drivers"
#endif
