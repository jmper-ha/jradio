#pragma once

#include "board_options.h"

/* Everything that follows from selecting one of the ST7796S panels. The shape
 * of this header is deliberately the same as display/ili9488.h, and the two are
 * worth reading side by side: the same glass size, the same module footprint,
 * the same board - and one difference that pays for the whole file.
 *
 * The ST7796S was fitted in place of the ILI9488 on 2026-09-11 and came up on
 * the ILI9488's own firmware, colours right and no offset, only turned half a
 * revolution. That is not luck: both controllers are 320x480 and both answer
 * the standard MIPI DCS commands the picture actually needs - SLPOUT, MADCTL,
 * COLMOD, CASET/RASET/RAMWR, INVON/INVOFF - and the ILI-specific rest of that
 * init sequence is either harmless or ignored, because the ST7796S keeps its
 * extended command set locked until 0xF0 opens it and the ILI9488 driver never
 * sends that.
 *
 * What it does cost, left that way, is a third of the bus:
 *
 *   - the ILI9488 cannot take RGB565 over SPI at all, so its driver converts
 *     every pixel to RGB666 and sends three bytes. The ST7796S takes 16-bit
 *     colour natively, so two bytes carry the same picture;
 *   - that conversion needs a buffer of its own, three bytes a pixel, out of
 *     MALLOC_CAP_DMA - the internal SRAM this firmware is short of. With the
 *     right driver there is no such buffer;
 *   - and the gamma tables the ILI9488 sequence writes are not this
 *     controller's. Espressif's driver sends Sitronix's own, behind the 0xF0
 *     unlock.
 *
 * So the panel is driven by esp_lcd_st7796 rather than by the code it happens
 * to boot with. */

/* The same module wiring as the ILI9488 it replaced, and it showed the same
 * answer: colours came out as drawn with the driver put in BGR. */
#define TFT_RGB_ORDER_BGR 1

/* Seen on the panel 2026-09-11: no negative, so no INVON. Sent as INVOFF
 * rather than skipped, so the state is the firmware's and not whatever the
 * controller reset into. */
#define TFT_INVERT_COLOR 0

/* Two bytes and a byte swap, which is what board_display_profile.h defaults
 * to, so neither TFT_PIXEL_WIRE_BYTES nor TFT_PIXEL_BYTE_SWAP appears here.
 * Worth saying out loud all the same: this is the line the whole change is
 * about, and it is the ILI9488's profile that is the exception. */

/* Ten rather than the default twenty, for the width and no longer for the
 * driver.
 *
 * A band is two static buffers, and at 480 px twenty rows is 19 KB each - 38 KB
 * of internal SRAM, which is more than the ILI9488 arrangement spent in total
 * (19 KB of bands plus its 14 KB conversion buffer). Ten rows at two bytes
 * costs 19 KB for the pair and nothing else, so the change hands about 14 KB of
 * internal SRAM back. The bus does not care: the same bytes go down it either
 * way, and a transaction's overhead is microseconds against the 1.9 ms a band
 * takes to clock out. */
#define LCD_DRAW_LINES 10

/* Geometry and the MADCTL baseline. Natively 320 wide by 480 tall, like every
 * other controller in this catalogue; landscape is the same glass with MV.
 *
 * TFT_MIRROR_X / TFT_MIRROR_Y are the baseline the device boots with, and the
 * user's two flip settings compose with them by XOR in
 * board_display_set_rotation(). */
#if DISPLAY == DISPLAY_ST7796S_320_480

#define TFT_WIDTH 320
#define TFT_HEIGHT 480
#define TFT_SWAP_XY 0
/* Derived, not measured - no portrait ST7796S has been built. The relation
 * carried across is the one this same glass showed under the ILI9488: going
 * from landscape to portrait flipped MX and left MY alone, because clearing MV
 * transposes rather than rotates and MX is what undoes the mirror that leaves
 * behind. Applied to the landscape pair below, that gives what stands here. If
 * a portrait build comes up mirrored, this is the line that was a guess. */
#define TFT_MIRROR_X 0
#define TFT_MIRROR_Y 1

#else

#define TFT_WIDTH 480
#define TFT_HEIGHT 320
#define TFT_SWAP_XY 1
/* Derived from what the panel showed on 2026-09-11 under the ILI9488 driver,
 * which is the only reading there was: the picture was right in every way
 * except turned half a revolution, so the MADCTL it was running wanted both
 * mirror bits set and had neither.
 *
 * That reading cannot be copied across as a pair of numbers, because the two
 * drivers do not mean the same thing by them: atanisoft's ILI9488 driver
 * *clears* MX for mirror_x true, where esp_lcd_st7796 sets it. The ILI9488
 * profile's landscape 1/0 therefore put MX=0, MY=0 on the glass; half a
 * revolution from there is MX=1, MY=1, and with a driver that means what it
 * says that is 1/1 here.
 *
 * Derived rather than measured, then - the arithmetic is small but it is
 * arithmetic, so it was written down as such and then looked at: the panel came
 * up the right way round on the first build, 2026-09-11. If a panel on another
 * board comes up upside down or mirrored, this pair is the first and probably
 * only thing to change. */
#define TFT_MIRROR_X 1
#define TFT_MIRROR_Y 1

#endif

/* The plain 0/1 the layout code tests, so no screen has to know which part
 * numbers exist to ask which way up this one is. */
#if DISPLAY == DISPLAY_ST7796S_320_480
#define BOARD_DISPLAY_PORTRAIT 1
#else
#define BOARD_DISPLAY_PORTRAIT 0
#endif

/* 40 MHz, inherited rather than measured: it is what the ILI9488 on this same
 * wiring was raised to and held, and this controller is the faster of the two.
 * The pins are the ESP32-S3's IOMUX ones for SPI2 (SCLK 12, MOSI 11, CS 10),
 * which is what makes a rate like this plausible rather than lucky. Raising it
 * further is worth doing with the panel in front of you - the failure is torn
 * or speckled pixels, not a dead screen - and this line is the first thing to
 * put back if another board shows either. */
#define DISPLAY_PIXEL_CLOCK_HZ (40 * 1000 * 1000)

/* The hardware scroll runs the other way on this glass: with the default
 * sign the screensaver's clock left by the far edge and came back in at the
 * near one. Checked on the panel 2026-09-12, landscape build; the portrait
 * build inherits it unchecked. */
#define TFT_SCROLL_REVERSED 1

/* Named for the log line and the error messages, so a boot log says which
 * panel the firmware was built for without anyone reading board_options.h. */
#define BOARD_PANEL_NAME "ST7796S"
