#include <assert.h>
#include <stdio.h>

#include "board_audio_format.h"
#include "board_display_profile.h"
#include "board_options.h"

/* These headers are plain data, so these are not tests of behaviour: they pin
 * the wiring against accidental edits and check the relations the drivers
 * depend on but cannot verify at runtime. The profile headers are included
 * too, because the invariants worth checking are precisely the ones that span
 * a choice in board_options.h and its consequences. */

static void test_a_selected_part_resolves_to_a_real_catalogue_entry(void)
{
    /* The point of naming parts in board_parts.h: a typo in a selection is an
     * unknown identifier and fails to compile, rather than evaluating to 0 and
     * silently selecting "none". */
    assert(DISPLAY != DISPLAY_NONE);
    assert(AUDIO_DAC != DAC_NONE);
}

static void test_display_group_is_complete_and_consistent(void)
{
    assert(DISPLAY == DISPLAY_ILI9341_320);
    /* Only SPI2 and SPI3 can drive a panel on this part; SPI1 is the flash bus. */
    assert(DISPLAY_SPI_PERIPHERAL == 2 || DISPLAY_SPI_PERIPHERAL == 3);
    assert(TFT_CS_GPIO == 10);
    assert(TFT_DC_GPIO == 47);
    assert(TFT_MOSI_GPIO == 11);
    assert(TFT_SCLK_GPIO == 12);
    assert(TFT_WIDTH == 320);
    assert(TFT_HEIGHT == 240);
    assert(TFT_RGB_ORDER_BGR == 1);
    assert(TFT_SWAP_XY == 1);
    assert(TFT_MIRROR_X == 0);
    assert(TFT_MIRROR_Y == 0);
    /* The panel is landscape, which only holds while the axes are swapped. */
    assert(TFT_SWAP_XY == 0 || TFT_WIDTH > TFT_HEIGHT);
}

static void test_backlight_duty_fits_the_pwm_timer(void)
{
    assert(TFT_BACKLIGHT_GPIO == 2);
    /* The pin is this board's; the PWM settings belong to the panel module and
     * live in its profile header. The pairing is what is worth checking. */
    assert(BACKLIGHT_PWM_HZ == 5000);
    /* LEDC tops out at 14 bits on this part, and board.c shifts by this count
     * into a uint32_t. */
    assert(BACKLIGHT_DUTY_BITS >= 1 && BACKLIGHT_DUTY_BITS <= 14);
}

static void test_control_pins_are_distinct(void)
{
    const int pins[] = {
        ENCODER_RIGHT_GPIO, ENCODER_LEFT_GPIO, ENCODER_BUTTON_GPIO,
        BUTTON_F1_GPIO, BUTTON_F2_GPIO, BUTTON_F3_GPIO,
        BUTTON_F4_GPIO,
    };
    const size_t count = sizeof(pins) / sizeof(pins[0]);
    for (size_t i = 0; i < count; ++i) {
        for (size_t j = i + 1U; j < count; ++j) {
            assert(pins[i] != pins[j]);
        }
    }
    /* Separate per group: the buttons need the internal pull-ups on top of
     * the external ones, the encoder does not and only pays contact current
     * for them. */
    assert(BUTTONS_USE_INTERNAL_PULLUPS == 1);
    assert(ENCODER_USE_INTERNAL_PULLUPS == 0);
}

static void test_audio_group_matches_the_fixed_i2s_slots(void)
{
    assert(AUDIO_DAC == DAC_PCM5102);
    assert(I2S_DOUT_GPIO == 16);
    assert(I2S_BCLK_GPIO == 18);
    assert(I2S_LRCK_GPIO == 17);
    /* The PCM5102 has no MCLK input on this board; enabling it would need a
     * fourth pin that does not exist. */
    assert(AUDIO_DAC_HAS_MCLK == 0);
    assert(AUDIO_DEFAULT_SAMPLE_RATE == 44100);
    assert(AUDIO_BITS_PER_SAMPLE == 16);
    assert(AUDIO_CHANNEL_COUNT == 2);
    /* The players expand mono into stereo rather than reconfiguring, so the
     * slot width and the sample width must agree. */
    assert(I2S_SLOT_BIT_WIDTH == AUDIO_BITS_PER_SAMPLE);
    assert(I2S_PRELOAD_SILENCE == 1);
}

static void test_dma_holds_enough_audio_to_ride_out_a_hiccup(void)
{
    assert(I2S_DMA_DESC_NUM == 8);
    assert(I2S_DMA_FRAME_NUM == 512);
    /* ~93 ms. This is the real tolerance to a source stall, so shrinking the
     * geometry silently weakens every buffer sized against it. */
    assert((I2S_DMA_DESC_NUM * I2S_DMA_FRAME_NUM * 1000U) /
               AUDIO_DEFAULT_SAMPLE_RATE >=
           90U);
    /* A full ring of zeros must not out-run the PCM5102's zero-data detect
     * (1024 frames), which is why output starts with a silent pre-roll. */
    assert(I2S_PRELOAD_SILENCE == 1 ||
           I2S_DMA_DESC_NUM * I2S_DMA_FRAME_NUM < 1024);
}

static void test_usb_group(void)
{
    assert(USB_DM_GPIO == 19);
    assert(USB_DP_GPIO == 20);
    assert(USB_DM_GPIO != USB_DP_GPIO);
    /* VBUS is not switched: recovery goes through the controller's root-port
     * power bit instead, so no GPIO is reserved for it. */
    assert(USB_VBUS_SWITCHED == 0);
}

static void test_no_pin_is_claimed_by_two_devices(void)
{
    /* The check that catches the actual mistake when moving a part: every
     * group is individually plausible, and only the whole set reveals a clash. */
    const int pins[] = {
        TFT_CS_GPIO, TFT_DC_GPIO, TFT_MOSI_GPIO, TFT_SCLK_GPIO,
        TFT_BACKLIGHT_GPIO,
        ENCODER_RIGHT_GPIO, ENCODER_LEFT_GPIO, ENCODER_BUTTON_GPIO,
        BUTTON_F1_GPIO, BUTTON_F2_GPIO, BUTTON_F3_GPIO,
        BUTTON_F4_GPIO,
        I2S_DOUT_GPIO, I2S_BCLK_GPIO, I2S_LRCK_GPIO,
        USB_DM_GPIO, USB_DP_GPIO,
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
    test_a_selected_part_resolves_to_a_real_catalogue_entry();
    test_display_group_is_complete_and_consistent();
    test_backlight_duty_fits_the_pwm_timer();
    test_control_pins_are_distinct();
    test_audio_group_matches_the_fixed_i2s_slots();
    test_dma_holds_enough_audio_to_ride_out_a_hiccup();
    test_usb_group();
    test_no_pin_is_claimed_by_two_devices();
    puts("board_options tests passed");
    return 0;
}
