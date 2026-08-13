#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "pcm_diagnostics.h"

static void test_peak_handles_s16le_extremes(void)
{
    const uint8_t pcm[] = {
        0x00, 0x00, /* 0 */
        0xff, 0x7f, /* 32767 */
        0x00, 0x80, /* -32768 */
        0x34, 0x12, /* 4660 */
    };

    assert(pcm_s16le_peak(pcm, sizeof(pcm)) == 32768U);
}

static void test_peak_ignores_incomplete_sample(void)
{
    const uint8_t pcm[] = {0x00, 0x01, 0xff};

    assert(pcm_s16le_peak(pcm, sizeof(pcm)) == 256U);
}

static void test_square_tone_is_stereo_s16le(void)
{
    uint8_t pcm[16] = {0};
    uint32_t phase = 0;

    assert(pcm_s16le_stereo_square_fill(pcm, sizeof(pcm), 8U, 2U, 1000, &phase) ==
           sizeof(pcm));
    const uint8_t expected[] = {
        0xe8, 0x03, 0xe8, 0x03, /* +1000, +1000 */
        0xe8, 0x03, 0xe8, 0x03, /* +1000, +1000 */
        0x18, 0xfc, 0x18, 0xfc, /* -1000, -1000 */
        0x18, 0xfc, 0x18, 0xfc, /* -1000, -1000 */
    };
    for (size_t index = 0; index < sizeof(pcm); ++index) {
        assert(pcm[index] == expected[index]);
    }
    assert(phase == 0U);
}

static void fill_frames(uint8_t *pcm, size_t frames, int16_t value)
{
    for (size_t frame = 0; frame < frames; ++frame) {
        const uint16_t encoded = (uint16_t)value;
        pcm[frame * 4U] = (uint8_t)encoded;
        pcm[frame * 4U + 1U] = (uint8_t)(encoded >> 8U);
        pcm[frame * 4U + 2U] = (uint8_t)encoded;
        pcm[frame * 4U + 3U] = (uint8_t)(encoded >> 8U);
    }
}

static void test_zero_run_measures_the_longest_silence_in_a_block(void)
{
    uint8_t pcm[40U * 4U];
    uint32_t carry = 0U;

    fill_frames(pcm, 40U, 1000);
    fill_frames(pcm + 5U * 4U, 7U, 0);   /* a run of 7 */
    fill_frames(pcm + 20U * 4U, 12U, 0); /* a longer run of 12 */
    assert(pcm_s16le_zero_frame_run(pcm, sizeof(pcm), &carry) == 12U);
    /* The block ends on audio, so nothing is left open for the next one. */
    assert(carry == 0U);
}

static void test_zero_run_spans_block_boundaries(void)
{
    /* The case a per-block check cannot see: neither block is entirely silent,
     * yet the DAC sees one uninterrupted run across the two. */
    uint8_t first[10U * 4U];
    uint8_t second[10U * 4U];
    uint32_t carry = 0U;

    fill_frames(first, 10U, 1000);
    fill_frames(first + 4U * 4U, 6U, 0); /* trailing run of 6 */
    fill_frames(second, 10U, 1000);
    fill_frames(second, 6U, 0); /* leading run of 6 */

    assert(pcm_s16le_zero_frame_run(first, sizeof(first), &carry) == 6U);
    assert(carry == 6U);
    assert(pcm_s16le_zero_frame_run(second, sizeof(second), &carry) == 12U);
    assert(carry == 0U);
}

static void test_zero_run_needs_both_channels_silent(void)
{
    /* One live channel still clocks data into the DAC, so a frame with any
     * non-zero sample must break the run. */
    uint8_t pcm[4U * 4U];
    uint32_t carry = 0U;

    fill_frames(pcm, 4U, 0);
    pcm[2U * 4U + 2U] = 0x01; /* right channel of the third frame */
    assert(pcm_s16le_zero_frame_run(pcm, sizeof(pcm), &carry) == 2U);
    assert(carry == 1U);
}

static void test_zero_run_survives_an_empty_or_missing_block(void)
{
    uint32_t carry = 5U;
    /* An empty block interrupts nothing: the run is still open. */
    assert(pcm_s16le_zero_frame_run(NULL, 0U, &carry) == 5U);
    assert(carry == 5U);
    assert(pcm_s16le_zero_frame_run((const uint8_t *)"", 0U, &carry) == 5U);
    assert(carry == 5U);
    assert(pcm_s16le_zero_frame_run((const uint8_t *)"", 0U, NULL) == 0U);
}

int main(void)
{
    test_peak_handles_s16le_extremes();
    test_peak_ignores_incomplete_sample();
    test_square_tone_is_stereo_s16le();
    test_zero_run_measures_the_longest_silence_in_a_block();
    test_zero_run_spans_block_boundaries();
    test_zero_run_needs_both_channels_silent();
    test_zero_run_survives_an_empty_or_missing_block();
    puts("pcm_diagnostics tests passed");
    return 0;
}
