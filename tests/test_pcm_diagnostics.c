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

int main(void)
{
    test_peak_handles_s16le_extremes();
    test_peak_ignores_incomplete_sample();
    test_square_tone_is_stereo_s16le();
    puts("pcm_diagnostics tests passed");
    return 0;
}
