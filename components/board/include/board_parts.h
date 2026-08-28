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
