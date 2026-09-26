#include "board_config.h"

#include <stdio.h>

#include "board_options.h"

/* The board this firmware was built for, out of board_options.h: what every
 * key a board.csv leaves out falls back to, and what the whole board falls
 * back to when the partition is empty or does not pass the rules.
 *
 * Every optional line of the header is optional here in the same way: a
 * #define that is missing is a part that is not fitted. */

#ifdef TFT_RESET_GPIO
#define COMPILED_TFT_RESET TFT_RESET_GPIO
#else
// Left out, the panel's RST is tied to the module's reset pad.
#define COMPILED_TFT_RESET BOARD_PIN_RESET
#endif

#define PIN(value) ((int8_t)(value))

void board_config_compiled(board_config_t *config)
{
    board_config_clear(config);
#ifdef DISPLAY
    config->display = (uint8_t)DISPLAY;
#endif
    config->tft_cs = PIN(TFT_CS_GPIO);
    config->tft_dc = PIN(TFT_DC_GPIO);
    config->tft_reset = PIN(COMPILED_TFT_RESET);
    config->tft_backlight = PIN(TFT_BACKLIGHT_GPIO);

    config->encoder_right = PIN(ENCODER_RIGHT_GPIO);
    config->encoder_left = PIN(ENCODER_LEFT_GPIO);
    config->encoder_button = PIN(ENCODER_BUTTON_GPIO);
    config->encoder_pullups = ENCODER_USE_INTERNAL_PULLUPS ? 1U : 0U;
#ifdef BUTTON_SLEEP_GPIO
    config->button_sleep = PIN(BUTTON_SLEEP_GPIO);
#endif
#ifdef BUTTON_QUICK_MENU_GPIO
    config->button_quick_menu = PIN(BUTTON_QUICK_MENU_GPIO);
#endif
#ifdef BUTTON_PREV_GPIO
    config->button_prev = PIN(BUTTON_PREV_GPIO);
#endif
#ifdef BUTTON_NEXT_GPIO
    config->button_next = PIN(BUTTON_NEXT_GPIO);
#endif
    config->buttons_pullups = BUTTONS_USE_INTERNAL_PULLUPS ? 1U : 0U;
#ifdef IR_RECEIVER_GPIO
    config->ir_receiver = PIN(IR_RECEIVER_GPIO);
#endif

#ifdef AUDIO_DAC
    config->dac = (uint8_t)AUDIO_DAC;
#endif
    config->i2s0_bclk = PIN(I2S_BCLK_GPIO);
    config->i2s0_lrck = PIN(I2S_LRCK_GPIO);
    config->i2s0_dout = PIN(I2S_DOUT_GPIO);
#ifdef AUDIO_DAC_MUTE_GPIO
    config->dac_mute = PIN(AUDIO_DAC_MUTE_GPIO);
#endif
#ifdef AUDIO_AMP_GPIO
    config->amp_enable = PIN(AUDIO_AMP_GPIO);
#endif
#ifdef AUDIO_AMP_ON_LEVEL
    config->amp_on_level = AUDIO_AMP_ON_LEVEL ? 1U : 0U;
#else
    config->amp_on_level = 1U;
#endif
#ifdef PERIPHERAL_POWER_GPIO
    config->peripheral_power = PIN(PERIPHERAL_POWER_GPIO);
#endif
#ifdef PERIPHERAL_POWER_ON_LEVEL
    config->peripheral_power_on_level = PERIPHERAL_POWER_ON_LEVEL ? 1U : 0U;
#else
    config->peripheral_power_on_level = 1U;
#endif

#if defined(USB_DP_GPIO) && defined(USB_DM_GPIO)
    config->usb_dp = PIN(USB_DP_GPIO);
    config->usb_dm = PIN(USB_DM_GPIO);
#endif
#ifdef SDC_CS_GPIO
    config->sd_cs = PIN(SDC_CS_GPIO);
    config->spi3_sclk = PIN(SDC_SCK_GPIO);
    config->spi3_mosi = PIN(SDC_MOSI_GPIO);
    config->spi3_miso = PIN(SDC_MISO_GPIO);
#endif
#if defined(BLUETOOTH) && BLUETOOTH == BLUETOOTH_JRADIO_BT
    config->bluetooth = BLUETOOTH_JRADIO_BT;
    config->uart1_tx = PIN(BT_UART_TX_GPIO);
    config->uart1_rx = PIN(BT_UART_RX_GPIO);
#endif
#if defined(YANDEX_MUSIC)
    config->yandex_music = YANDEX_MUSIC == FEATURE_ON ? 1U : 0U;
#endif
#if defined(DLNA)
    config->dlna = DLNA == FEATURE_ON ? 1U : 0U;
#endif
}
