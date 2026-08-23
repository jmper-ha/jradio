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
/* ILI9341 controller driving a 320x240 panel over SPI. */
#define DISPLAY_ILI9341_320 1

#define DAC_NONE 0
/* PCM5102 / PCM5102A I2S stereo DAC, no MCLK input, line level out. */
#define DAC_PCM5102 1

/* For the options that are not a part but a yes/no: whether a feature is built
 * into this firmware at all. Prefixed rather than plain ON/OFF, which are far
 * too common as identifiers to take over as macros. */
#define FEATURE_OFF 0
#define FEATURE_ON 1
