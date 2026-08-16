#include <assert.h>
#include <stdio.h>

#include "board_options.h"

/* board_options.h is plain data, so these are not tests of behaviour: they pin
 * the wiring against accidental edits and check the relations between options
 * that the drivers depend on but cannot verify at runtime. */

static void test_display_group_is_complete_and_consistent(void)
{
    assert(BOARD_DISPLAY_CONTROLLER == BOARD_DISPLAY_ILI9341);
    /* Only SPI2 and SPI3 can drive a panel on this part; SPI1 is the flash bus. */
    assert(BOARD_DISPLAY_SPI_PERIPHERAL == 2 || BOARD_DISPLAY_SPI_PERIPHERAL == 3);
    assert(BOARD_TFT_CS_GPIO == 10);
    assert(BOARD_TFT_DC_GPIO == 47);
    assert(BOARD_TFT_MOSI_GPIO == 11);
    assert(BOARD_TFT_SCLK_GPIO == 12);
    assert(BOARD_TFT_WIDTH == 320);
    assert(BOARD_TFT_HEIGHT == 240);
    assert(BOARD_TFT_RGB_ORDER_BGR == 1);
    assert(BOARD_TFT_SWAP_XY == 1);
    assert(BOARD_TFT_MIRROR_X == 0);
    assert(BOARD_TFT_MIRROR_Y == 0);
    assert(BOARD_LCD_WAIT_FOR_TRANSFER == 1);
    /* The panel is landscape, which only holds while the axes are swapped. */
    assert(BOARD_TFT_SWAP_XY == 0 || BOARD_TFT_WIDTH > BOARD_TFT_HEIGHT);
}

static void test_backlight_duty_fits_the_pwm_timer(void)
{
    assert(BOARD_TFT_BACKLIGHT_GPIO == 2);
    assert(BOARD_BACKLIGHT_PWM_HZ == 5000);
    /* LEDC tops out at 14 bits on this part, and board.c shifts by this count
     * into a uint32_t. */
    assert(BOARD_BACKLIGHT_DUTY_BITS >= 1 && BOARD_BACKLIGHT_DUTY_BITS <= 14);
}

static void test_control_pins_are_distinct(void)
{
    const int pins[] = {
        BOARD_ENCODER_RIGHT_GPIO, BOARD_ENCODER_LEFT_GPIO, BOARD_ENCODER_BUTTON_GPIO,
        BOARD_BUTTON_F1_GPIO, BOARD_BUTTON_F2_GPIO, BOARD_BUTTON_F3_GPIO,
        BOARD_BUTTON_F4_GPIO,
    };
    const size_t count = sizeof(pins) / sizeof(pins[0]);
    for (size_t i = 0; i < count; ++i) {
        for (size_t j = i + 1U; j < count; ++j) {
            assert(pins[i] != pins[j]);
        }
    }
    assert(BOARD_INPUT_USE_INTERNAL_PULLUPS == 1);
}

static void test_debounce_is_a_whole_number_of_polls(void)
{
    /* board_input.c derives its sample count by integer division, so a debounce
     * window that is not a multiple of the poll interval silently rounds down
     * and filters less than the option claims. */
    assert(BOARD_INPUT_POLL_MS > 0);
    assert(BOARD_INPUT_DEBOUNCE_MS % BOARD_INPUT_POLL_MS == 0);
    assert(BOARD_INPUT_DEBOUNCE_MS / BOARD_INPUT_POLL_MS >= 2);
    /* The hold has to outlast the debounce, or F1 would register as held. */
    assert(BOARD_INPUT_F1_HOLD_MS > BOARD_INPUT_DEBOUNCE_MS);
}

static void test_audio_group_matches_the_fixed_i2s_slots(void)
{
    assert(BOARD_AUDIO_DAC == BOARD_AUDIO_DAC_PCM5102);
    assert(BOARD_I2S_DOUT_GPIO == 16);
    assert(BOARD_I2S_BCLK_GPIO == 18);
    assert(BOARD_I2S_LRCK_GPIO == 17);
    /* The PCM5102 has no MCLK input on this board; enabling it would need a
     * fourth pin that does not exist. */
    assert(BOARD_AUDIO_DAC_HAS_MCLK == 0);
    assert(BOARD_AUDIO_DEFAULT_SAMPLE_RATE == 44100);
    assert(BOARD_AUDIO_BITS_PER_SAMPLE == 16);
    assert(BOARD_AUDIO_CHANNEL_COUNT == 2);
    /* The players expand mono into stereo rather than reconfiguring, so the
     * slot width and the sample width must agree. */
    assert(BOARD_I2S_SLOT_BIT_WIDTH == BOARD_AUDIO_BITS_PER_SAMPLE);
    assert(BOARD_I2S_AUTO_CLEAR_AFTER_CB == 1);
    assert(BOARD_I2S_PRELOAD_SILENCE == 1);
}

static void test_dma_holds_enough_audio_to_ride_out_a_hiccup(void)
{
    assert(BOARD_I2S_DMA_DESC_NUM == 8);
    assert(BOARD_I2S_DMA_FRAME_NUM == 512);
    /* ~93 ms. This is the real tolerance to a source stall, so shrinking the
     * geometry silently weakens every buffer sized against it. */
    assert((BOARD_I2S_DMA_DESC_NUM * BOARD_I2S_DMA_FRAME_NUM * 1000U) /
               BOARD_AUDIO_DEFAULT_SAMPLE_RATE >=
           90U);
    /* A full ring of zeros must not out-run the PCM5102's zero-data detect
     * (1024 frames), which is why output starts with a silent pre-roll. */
    assert(BOARD_I2S_PRELOAD_SILENCE == 1 ||
           BOARD_I2S_DMA_DESC_NUM * BOARD_I2S_DMA_FRAME_NUM < 1024);
}

static void test_usb_group(void)
{
    assert(BOARD_USB_DM_GPIO == 19);
    assert(BOARD_USB_DP_GPIO == 20);
    assert(BOARD_USB_DM_GPIO != BOARD_USB_DP_GPIO);
    /* VBUS is not switched: recovery goes through the controller's root-port
     * power bit instead, so no GPIO is reserved for it. */
    assert(BOARD_USB_VBUS_SWITCHED == 0);
}

static void test_no_pin_is_claimed_by_two_devices(void)
{
    /* The check that catches the actual mistake when moving a part: every
     * group is individually plausible, and only the whole set reveals a clash. */
    const int pins[] = {
        BOARD_TFT_CS_GPIO, BOARD_TFT_DC_GPIO, BOARD_TFT_MOSI_GPIO, BOARD_TFT_SCLK_GPIO,
        BOARD_TFT_BACKLIGHT_GPIO,
        BOARD_ENCODER_RIGHT_GPIO, BOARD_ENCODER_LEFT_GPIO, BOARD_ENCODER_BUTTON_GPIO,
        BOARD_BUTTON_F1_GPIO, BOARD_BUTTON_F2_GPIO, BOARD_BUTTON_F3_GPIO,
        BOARD_BUTTON_F4_GPIO,
        BOARD_I2S_DOUT_GPIO, BOARD_I2S_BCLK_GPIO, BOARD_I2S_LRCK_GPIO,
        BOARD_USB_DM_GPIO, BOARD_USB_DP_GPIO,
    };
    const size_t count = sizeof(pins) / sizeof(pins[0]);
    for (size_t i = 0; i < count; ++i) {
        assert(pins[i] >= 0 && pins[i] <= 48);
        for (size_t j = i + 1U; j < count; ++j) {
            assert(pins[i] != pins[j]);
        }
    }
}

int main(void)
{
    test_display_group_is_complete_and_consistent();
    test_backlight_duty_fits_the_pwm_timer();
    test_control_pins_are_distinct();
    test_debounce_is_a_whole_number_of_polls();
    test_audio_group_matches_the_fixed_i2s_slots();
    test_dma_holds_enough_audio_to_ride_out_a_hiccup();
    test_usb_group();
    test_no_pin_is_claimed_by_two_devices();
    puts("board_options tests passed");
    return 0;
}
