#pragma once

#include "board_options.h"

/* Everything that follows from selecting one of the ILI9488 panels. The shape
 * of this header is deliberately the same as display/ili9341.h and
 * display/st7789.h, so the three can be read side by side - but this part is
 * the one that breaks an assumption the other two share, and the break is
 * TFT_PIXEL_WIRE_BYTES below.
 *
 * The landscape part was brought up on a panel on 2026-09-04 with a standalone
 * probe: it drew a frame that had to close on all four edges and four
 * differently coloured corners, and the answers came back off the glass. The
 * panel bus has no MISO, so nothing here could be read out of the controller -
 * every value is what the screen showed, and that is the only way it can be.
 * Portrait was brought up the same way on 2026-09-05, on the same glass. */

/* Measured 2026-09-04: red, green, blue and yellow all came back as drawn with
 * the driver put in BGR, the same way round as the ILI9341. */
#define TFT_RGB_ORDER_BGR 1

/* Measured 2026-09-04: the ramp and the white ground came out as drawn, so
 * this panel needs no INVON. Sent as INVOFF rather than skipped, so the state
 * is the firmware's and not whatever the controller reset into. */
#define TFT_INVERT_COLOR 0

/* Three bytes per pixel, and not a tuning knob.
 *
 * Over SPI the ILI9488 accepts only 18-bit colour: 16 bpp exists in the
 * controller but is reachable only through the parallel interface. So the
 * vendor driver takes RGB565 in and converts each pixel to RGB666 on its way
 * out, and two things follow that the rest of the firmware has to know about.
 * The number is stated here because board.c sizes the SPI transfer from it,
 * and because it is the reason this panel costs half again as much bus time as
 * the others for the same picture. */
#define TFT_PIXEL_WIRE_BYTES 3

/* And the second consequence: the driver reads the incoming buffer as native
 * uint16_t to do that conversion, so the big-endian swap the ILI9341 and the
 * ST7789 need before their bytes go out would arrive here as noise. */
#define TFT_PIXEL_BYTE_SWAP 0

/* Ten rather than the twenty the 16-bit panels use. A band is a buffer, and
 * two of them are static: at 480 px wide, twenty lines would be 19 KB each
 * where 320 px at twenty lines is 12 KB - and the vendor driver allocates a
 * conversion buffer of three bytes per pixel on top, out of MALLOC_CAP_DMA,
 * which on this chip means the internal SRAM that TLS and the decoder task
 * stacks are already competing for. Ten lines at 480 px costs the same bytes
 * per band as the panel this board shipped with. */
#define LCD_DRAW_LINES 10

/* Geometry and the MADCTL baseline. Natively 320 wide by 480 tall, like the
 * other two controllers here; landscape is the same glass with MADCTL MV.
 *
 * TFT_MIRROR_X / TFT_MIRROR_Y are the baseline the device boots with, and the
 * user's two flip settings compose with them by XOR in
 * board_display_set_rotation(). Note that this is the first panel in the
 * catalogue whose baseline is not 0/0: the probe's first pass came up a
 * quarter turn out, and MX is what the transpose left behind. */
#if DISPLAY == DISPLAY_ILI9488_320_480

#define TFT_WIDTH 320
#define TFT_HEIGHT 480
#define TFT_SWAP_XY 0
/* Confirmed on the panel 2026-09-05. This pair is what clearing MV from the
 * measured landscape baseline suggests, and it was written down as a guess
 * because on the ILI9341 that same reasoning came out wrong - so it stood
 * until somebody had looked. Somebody has: the firmware was built for this
 * shape and the picture came up the right way up. */
#define TFT_MIRROR_X 0
#define TFT_MIRROR_Y 0

#else

#define TFT_WIDTH 480
#define TFT_HEIGHT 320
#define TFT_SWAP_XY 1
/* Measured 2026-09-04. The probe cycled all four combinations six seconds
 * apart, marking each with that many squares across the middle so the count
 * named the combination without depending on the orientation being tested;
 * two squares is the one that put red in the top-left corner. */
#define TFT_MIRROR_X 1
#define TFT_MIRROR_Y 0

#endif

/* The plain 0/1 the layout code tests, so no screen has to know which part
 * numbers exist to ask which way up this one is. */
#if DISPLAY == DISPLAY_ILI9488_320_480
#define BOARD_DISPLAY_PORTRAIT 1
#else
#define BOARD_DISPLAY_PORTRAIT 0
#endif

/* 40 MHz. Brought up at 20 on 2026-09-04 - what this board's ILI9341 runs at,
 * kept deliberately so a panel that stayed dark could not blame the clock -
 * and raised on 2026-09-05 once there was a picture to compare.
 *
 * The reason to raise it is that this panel spends three bytes on a pixel: a
 * full frame is 460 800 bytes against the 153 600 of the screen this board
 * shipped with, and at 20 MHz that is 184 ms of a 344 ms screen change. The
 * measurement either side, on the player screen's first draw:
 *
 *     20 MHz   344 ms, of which 252 in the flush
 *     40 MHz   249 ms, of which 157 in the flush
 *
 * The 95 ms is the wire time halving and nothing else: what the flush costs
 * beyond it, 65-68 ms of copying and converting 153 600 pixels, does not move,
 * and neither does the 92 ms LVGL spends producing them.
 *
 * Above the ILI9488's own 50 ns write cycle, which is 20 MHz, so this is the
 * panel being run past its datasheet and holding - the SPI2 pins here are the
 * ESP32-S3's IOMUX ones (SCLK 12, MOSI 11, CS 10), which is what makes that
 * plausible rather than lucky. Nothing here was seen on a scope; what was seen
 * is the picture, which is the check that matters for a bus that is written to
 * and never read. If a panel on another board shows torn or speckled pixels,
 * this line is the first thing to put back to 20. */
#define DISPLAY_PIXEL_CLOCK_HZ (40 * 1000 * 1000)

/* Named for the log line and the error messages, so a boot log says which
 * panel the firmware was built for without anyone reading board_options.h. */
#define BOARD_PANEL_NAME "ILI9488"
