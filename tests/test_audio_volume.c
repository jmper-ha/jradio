#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "audio_volume.h"

static void test_full_volume_is_bit_exact(void)
{
    /* The point of skipping the multiply at the top of the scale: at full
     * volume the samples handed to the DAC must be the ones the decoder
     * produced, not a rounded copy of them. */
    assert(audio_volume_gain(100U) == AUDIO_VOLUME_UNITY);
    assert(audio_volume_gain(120U) == AUDIO_VOLUME_UNITY);

    const uint8_t source[] = {0x34, 0x12, 0xff, 0x7f, 0x00, 0x80, 0x01, 0x00};
    uint8_t out[sizeof(source)];
    audio_volume_apply(source, out, sizeof(source), AUDIO_VOLUME_UNITY);
    assert(memcmp(source, out, sizeof(source)) == 0);
}

static void test_zero_is_silence_not_the_bottom_of_the_curve(void)
{
    /* A volume control has to have an off position. The quietest step above
     * it is still audible, which is what makes 0 a distinct state. */
    assert(audio_volume_gain(0U) == 0U);
    assert(audio_volume_gain(1U) > 0U);

    const uint8_t source[] = {0xff, 0x7f, 0x00, 0x80};
    uint8_t out[sizeof(source)];
    audio_volume_apply(source, out, sizeof(source), 0U);
    for (size_t index = 0; index < sizeof(out); ++index) {
        assert(out[index] == 0U);
    }
}

static void test_the_curve_rises_without_a_dip(void)
{
    /* A dip anywhere would be a click, or a step where turning the knob up
     * makes the music quieter. */
    uint16_t previous = 0U;
    for (unsigned int percent = 0U; percent <= 100U; ++percent) {
        const uint16_t gain = audio_volume_gain((uint8_t)percent);
        assert(gain >= previous);
        assert(gain <= AUDIO_VOLUME_UNITY);
        previous = gain;
    }
}

static void test_the_curve_is_logarithmic_not_linear(void)
{
    /* Halfway on a linear control is 6 dB down and sounds barely quieter, so
     * the whole useful range would be crammed into the top few percent. */
    const uint16_t half = audio_volume_gain(50U);
    assert(half < AUDIO_VOLUME_UNITY / 4U);

    /* Equal steps in percent are equal steps in decibels, which means equal
     * ratios: each 20 percent down should roughly halve the amplitude
     * squared. Checking the ratio holds across the range is what separates a
     * log curve from a bent linear one. */
    const uint16_t at80 = audio_volume_gain(80U);
    const uint16_t at60 = audio_volume_gain(60U);
    const uint16_t at40 = audio_volume_gain(40U);
    const unsigned int ratio_high = (unsigned int)at80 * 100U / at60;
    const unsigned int ratio_low = (unsigned int)at60 * 100U / at40;
    assert(ratio_high > 230U && ratio_high < 270U);
    assert(ratio_low > 230U && ratio_low < 270U);
}

static void test_scaling_halves_a_known_sample(void)
{
    /* -6 dB is a gain of 16384: a sample of 20000 has to come back as 10000,
     * not 9999 - that is the rounding the comment in the source is about. */
    const uint8_t source[] = {0x20, 0x4e, 0xe0, 0xb1}; /* +20000, -20000 */
    uint8_t out[sizeof(source)];
    audio_volume_apply(source, out, sizeof(source), 16384U);

    const int16_t left = (int16_t)((uint16_t)out[0] | ((uint16_t)out[1] << 8U));
    const int16_t right = (int16_t)((uint16_t)out[2] | ((uint16_t)out[3] << 8U));
    assert(left == 10000);
    assert(right == -10000);
}

static void test_rounding_does_not_pull_towards_zero(void)
{
    /* Truncation would bias every sample towards DC, which on a quiet passage
     * is audible as distortion rather than as a level change. A sample of 3 at
     * half gain is 1.5 and must not become 1 in one direction and -1 in the
     * other. */
    const uint8_t source[] = {0x03, 0x00, 0xfd, 0xff}; /* +3, -3 */
    uint8_t out[sizeof(source)];
    audio_volume_apply(source, out, sizeof(source), 16384U);

    const int16_t left = (int16_t)((uint16_t)out[0] | ((uint16_t)out[1] << 8U));
    const int16_t right = (int16_t)((uint16_t)out[2] | ((uint16_t)out[3] << 8U));
    assert(left == 2);
    assert(right == -2);
}

static void test_the_negative_limit_cannot_overflow(void)
{
    /* -32768 has no positive counterpart; at unity the scaling must leave it
     * alone rather than clamp it to +32767 and inverted. */
    const uint8_t source[] = {0x00, 0x80, 0x00, 0x80};
    uint8_t out[sizeof(source)];
    audio_volume_apply(source, out, sizeof(source), AUDIO_VOLUME_UNITY);
    const int16_t left = (int16_t)((uint16_t)out[0] | ((uint16_t)out[1] << 8U));
    assert(left == INT16_MIN);
}

static void test_an_odd_trailing_byte_is_carried_through(void)
{
    /* Half a sample at the end of a block. Dropping it would shift every
     * following block by a byte and swap the channels for the rest of the
     * track. */
    const uint8_t source[] = {0x00, 0x40, 0x7b};
    uint8_t out[sizeof(source)] = {0};
    audio_volume_apply(source, out, sizeof(source), 16384U);
    assert(out[2] == 0x7b);

    audio_volume_apply(NULL, out, sizeof(out), 16384U);
    audio_volume_apply(source, NULL, sizeof(source), 16384U);
}

static void test_stepping_stops_at_the_ends(void)
{
    assert(audio_volume_step(50U, 5) == 55U);
    assert(audio_volume_step(50U, -5) == 45U);

    /* The encoder can deliver several clicks between polls, so an overshoot
     * has to clamp rather than wrap into a full-blast surprise. */
    assert(audio_volume_step(3U, -40) == 0U);
    assert(audio_volume_step(98U, 40) == 100U);
    assert(audio_volume_step(0U, -1) == 0U);
    assert(audio_volume_step(100U, 1) == 100U);
    assert(audio_volume_step(0U, 1) == 1U);
}

static void test_the_fade_starts_at_silence_and_ends_at_the_setting(void)
{
    const uint16_t gain = audio_volume_gain(100U);
    // The first frame after silence is the one that clicks, so it is the one
    // that has to be zero.
    assert(audio_volume_fade_gain(gain, 0U, AUDIO_VOLUME_FADE_FRAMES) == 0U);
    assert(audio_volume_fade_gain(gain, AUDIO_VOLUME_FADE_FRAMES / 2U,
                                  AUDIO_VOLUME_FADE_FRAMES) == gain / 2U);
    assert(audio_volume_fade_gain(gain, AUDIO_VOLUME_FADE_FRAMES,
                                  AUDIO_VOLUME_FADE_FRAMES) == gain);
    /* Past the end it stays at the setting, so the caller can keep counting
     * frames without a separate "done" flag. */
    assert(audio_volume_fade_gain(gain, AUDIO_VOLUME_FADE_FRAMES * 100U,
                                  AUDIO_VOLUME_FADE_FRAMES) == gain);
    // No fade asked for is not the same as a fade of length zero divided by.
    assert(audio_volume_fade_gain(gain, 0U, 0U) == gain);
    // The fade scales whatever the listener set, rather than overriding it.
    const uint16_t quiet = audio_volume_gain(40U);
    assert(audio_volume_fade_gain(quiet, AUDIO_VOLUME_FADE_FRAMES,
                                  AUDIO_VOLUME_FADE_FRAMES) == quiet);
    assert(audio_volume_fade_gain(quiet, AUDIO_VOLUME_FADE_FRAMES / 4U,
                                  AUDIO_VOLUME_FADE_FRAMES) < quiet);
}

static void test_a_ramp_moves_within_the_block_not_between_blocks(void)
{
    /* Four frames of full-scale left, silent right. A per-block fade would
     * scale all four the same and leave a step at the block edge; the point of
     * the ramp is that the step is spread across the frames. */
    uint8_t source[4U * AUDIO_VOLUME_FRAME_BYTES];
    uint8_t destination[sizeof(source)];
    for (size_t frame = 0; frame < 4U; ++frame) {
        const size_t offset = frame * AUDIO_VOLUME_FRAME_BYTES;
        source[offset] = 0x00U;
        source[offset + 1U] = 0x40U; /* 16384 */
        source[offset + 2U] = 0x00U;
        source[offset + 3U] = 0x40U;
    }
    audio_volume_apply_ramp(source, destination, sizeof(source), 0U,
                            (uint16_t)AUDIO_VOLUME_UNITY);
    int16_t previous = -1;
    for (size_t frame = 0; frame < 4U; ++frame) {
        const size_t offset = frame * AUDIO_VOLUME_FRAME_BYTES;
        const int16_t left = (int16_t)((uint16_t)destination[offset] |
                                       ((uint16_t)destination[offset + 1U] << 8U));
        const int16_t right = (int16_t)((uint16_t)destination[offset + 2U] |
                                        ((uint16_t)destination[offset + 3U] << 8U));
        // Both channels of a frame share the gain, or the ramp would swing the
        // stereo image as it runs.
        assert(left == right);
        assert(left > previous);
        previous = left;
    }
    assert(previous < 16384);
    // A flat "ramp" is the plain scaling every other block gets.
    uint8_t flat[sizeof(source)];
    audio_volume_apply_ramp(source, flat, sizeof(source), (uint16_t)AUDIO_VOLUME_UNITY,
                            (uint16_t)AUDIO_VOLUME_UNITY);
    assert(memcmp(flat, source, sizeof(source)) == 0);
}

int main(void)
{
    test_full_volume_is_bit_exact();
    test_zero_is_silence_not_the_bottom_of_the_curve();
    test_the_curve_rises_without_a_dip();
    test_the_curve_is_logarithmic_not_linear();
    test_scaling_halves_a_known_sample();
    test_rounding_does_not_pull_towards_zero();
    test_the_negative_limit_cannot_overflow();
    test_an_odd_trailing_byte_is_carried_through();
    test_stepping_stops_at_the_ends();
    test_the_fade_starts_at_silence_and_ends_at_the_setting();
    test_a_ramp_moves_within_the_block_not_between_blocks();
    puts("audio_volume tests passed");
    return 0;
}
