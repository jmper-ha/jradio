#pragma once

/* Catalogue of the external parts this firmware has drivers for, and the
 * vocabulary board_options.h uses to select them.
 *
 * board_options.h picks one of each with a single line; the matching profile
 * header then supplies everything that follows from that choice. Keeping the
 * identifiers here rather than in board_options.h means a typo in a selection
 * is an unknown name and fails, instead of quietly evaluating to 0 in the
 * preprocessor the way an undefined identifier would.
 *
 * A part number carries the variant that changes the driver's behaviour - the
 * ILI9341 exists in panels of different resolutions, and the resolution is not
 * something the board wiring can tell us.
 */

#define DISPLAY_NONE 0
/* ILI9341 controller over SPI, in the two ways this firmware can drive it.
 *
 * The same module both times - the resolution written into the name is the
 * panel as the firmware addresses it, not two different parts. Which way up it
 * is mounted decides the layout of all six screens, and like the resolution it
 * is not something the wiring can tell us, so it belongs in the part number
 * for the same reason the resolution already did.
 *
 * A build option and deliberately not a setting: every screen's geometry is
 * compiled for one shape, and a box is mounted one way round once. See
 * board_display_profile.h for everything the choice decides. */
#define DISPLAY_ILI9341_320_240 1
#define DISPLAY_ILI9341_240_320 2
/* ST7789 controller over SPI, the same two ways. Read as one part with two
 * orientations, exactly like the pair above.
 *
 * Not yet seen on a panel: display/st7789.h has the constants the datasheet
 * and the usual wiring of these modules give, and says which of them can only
 * be settled by looking at the screen. The driver itself is ESP-IDF's own -
 * esp_lcd carries ST7789 in the box, unlike the ILI9341, which arrives as a
 * managed component. */
#define DISPLAY_ST7789_320_240 3
#define DISPLAY_ST7789_240_320 4
/* ST7789 controller on the 1.9" 320x170 module - the same controller, the
 * same wiring, and a third of the glass missing: the controller's memory is
 * 240 columns and the panel shows the middle 170 of them, so the driver
 * addresses the picture 35 columns in. Landscape only; nobody has mounted one
 * on end, and the layout a screen this short needs was drawn for lying
 * down. The number is out of sequence with its two siblings because it was
 * added after the two 480x320 parts. */
#define DISPLAY_ST7789_320_170 9
/* ILI9488 controller over SPI, on the 480x320 panel. Read the same way as the
 * pairs above - one part, two orientations - with one difference that is not
 * cosmetic: this controller cannot be driven at 16 bits per pixel over SPI at
 * all. Three bytes go down the wire for every pixel, which is why its profile
 * carries TFT_PIXEL_WIRE_BYTES and why an ILI9341 driver cannot be pointed at
 * it however similar the wiring looks.
 *
 * Measured on the panel 2026-09-04: 320x480 native, BGR, no inversion, 20 MHz.
 * The 480x320 name is the one this board uses - the module is mounted lying
 * down, which the probe established by drawing a pattern and being told which
 * corner each colour landed in. */
#define DISPLAY_ILI9488_480_320 5
#define DISPLAY_ILI9488_320_480 6
/* ST7796S controller, on the same 480x320 module footprint as the ILI9488 -
 * one part, two orientations, read like the pairs above.
 *
 * Fitted in place of the ILI9488 on 2026-09-11 and it came up on the ILI9488's
 * own firmware: same glass size, and both controllers answer the standard MIPI
 * DCS commands a picture needs. That makes it a drop-in and not a driver.
 * Driven by its own because this one takes 16-bit colour over SPI, where the
 * ILI9488 takes only 18 - two bytes a pixel instead of three, and no conversion
 * buffer in internal SRAM. See display/st7796s.h for the arithmetic.
 *
 * The 480x320 name is the one this board uses: the module is mounted lying
 * down, in the same enclosure and the same way round as the panel it
 * replaced. */
#define DISPLAY_ST7796S_480_320 7
#define DISPLAY_ST7796S_320_480 8

#define DAC_NONE 0
/* PCM5102 / PCM5102A I2S stereo DAC, no MCLK input, line level out. */
#define DAC_PCM5102 1

#define FM_TUNER_NONE 0
/* RDA5807M receiver module: FM band, I2C control, line level out into the
 * same amplifier as the DAC. No driver is written yet - the name exists so
 * fitting one is a line in board_options.h and not a search for every place
 * that has to learn about it. */
#define FM_TUNER_RDA5807 1

#define BLUETOOTH_NONE 0
/* Classic Bluetooth A2DP sink. Named but unreachable on this part: the
 * ESP32-S3 radio does BLE only, so an audio sink needs a module of its own
 * feeding I2S or UART. Selecting it on an S3 is a mistake worth catching by
 * name rather than by silence. */
#define BLUETOOTH_A2DP_SINK 1

/* For the options that are not a part but a yes/no: whether a feature is built
 * into this firmware at all. Prefixed rather than plain ON/OFF, which are far
 * too common as identifiers to take over as macros. */
#define FEATURE_OFF 0
#define FEATURE_ON 1
