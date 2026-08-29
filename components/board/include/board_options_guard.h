#pragma once

/* Checks that nothing else has taken one of board_options.h's names.
 *
 * Those names are deliberately unprefixed - TFT_CS_GPIO, DISPLAY, AUDIO_DAC -
 * which puts them in the same namespace as ESP-IDF's. Nothing collides today,
 * but a future IDF release defining e.g. its own I2S_DOUT_GPIO would silently
 * win or lose depending on include order. Testing them here, before
 * board_options.h defines anything, turns that into a build error naming the
 * option. It is also why board_options.h is included *after* the driver
 * headers, as every file here does.
 *
 * The check lives in a header of its own so that board_options.h stays what it
 * looks like: a list of parts and pins to edit, with no preprocessor
 * machinery at the top for a reader to work out before reaching the first
 * GPIO number.
 *
 * #pragma once is load-bearing rather than habit: on a second inclusion the
 * names below are defined by board_options.h itself, and the check would fire
 * on its own work.
 */

#if defined(TFT_CS_GPIO) || defined(I2S_DOUT_GPIO) || defined(USB_DP_GPIO) || \
    defined(ENCODER_LEFT_GPIO) || defined(TFT_BACKLIGHT_GPIO) ||              \
    defined(BUTTON_F1_GPIO) || defined(DISPLAY) || defined(AUDIO_DAC) ||      \
    defined(SDC_CS_GPIO) || defined(YANDEX_MUSIC) || defined(FM_TUNER) ||     \
    defined(BLUETOOTH) || defined(DLNA)
#error "board_options.h: a name here is already defined elsewhere - most likely \
an ESP-IDF header now uses it. Re-prefix the affected option in this file and \
at its use sites."
#endif
