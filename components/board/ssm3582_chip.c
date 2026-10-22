#include "ssm3582_chip.h"

#include <stddef.h>

/* Breakpoints of the volume curve, in the chip's steps of 0.375 dB below
 * unity. The percentages and the decibels are audio_volume.c's, taken
 * through the ladder: -4 dB is 11 steps, -20 dB is 53, -60 dB is 160.
 * The curve reaches -71.25 dB and a hard mute at 0xFF; -60 is where the
 * digital curve ends and the listener's, and every step below it is
 * quieter than anything the knob can ask for. */
static const struct {
    uint8_t percent;
    uint8_t steps;
} s_curve[] = {
    {100U, 0U},    /*   0 dB */
    {90U, 11U},    /*  -4 dB */
    {80U, 21U},    /*  -8 dB */
    {70U, 32U},    /* -12 dB */
    {60U, 43U},    /* -16 dB */
    {50U, 53U},    /* -20 dB */
    {40U, 64U},    /* -24 dB */
    {30U, 75U},    /* -28 dB */
    {20U, 96U},    /* -36 dB */
    {15U, 109U},   /* -41 dB */
    {10U, 123U},   /* -46 dB */
    {5U, 141U},    /* -53 dB */
    {1U, 160U},    /* -60 dB */
};

uint8_t ssm3582_volume_register(uint8_t percent)
{
    if (percent == 0U) return SSM3582_VOL_MUTE;
    if (percent >= 100U) return SSM3582_VOL_UNITY;

    const size_t count = sizeof(s_curve) / sizeof(s_curve[0]);
    for (size_t index = 1U; index < count; ++index) {
        if (percent > s_curve[index].percent) {
            /* Between s_curve[index-1] (louder, fewer steps) and
             * s_curve[index] (quieter, more) - the same walk
             * audio_volume_gain() makes over its gains, in the other
             * direction because a bigger register is a quieter sound. */
            const uint32_t span = (uint32_t)(s_curve[index - 1U].percent - s_curve[index].percent);
            const uint32_t into = (uint32_t)(percent - s_curve[index].percent);
            const uint32_t range = (uint32_t)(s_curve[index].steps - s_curve[index - 1U].steps);
            return (uint8_t)(SSM3582_VOL_UNITY + s_curve[index].steps -
                             (uint8_t)((into * range) / span));
        }
    }
    return (uint8_t)(SSM3582_VOL_UNITY + s_curve[count - 1U].steps);
}

uint8_t ssm3582_dac_fs_bits(uint32_t sample_rate)
{
    /* The bands as the chip defines them. Two of its codes cover one
     * stretch apiece: 2 up to and including 48 kHz, 5 from there to
     * 72 kHz, and 3 from there to 96 kHz - where 5 and 2 overlap the
     * datasheet's own 48-72 and 32-48 statements, one value carries a
     * whole stretch of rates, and every rate ends up in a band that
     * names it. */
    if (sample_rate >= 128000U && sample_rate <= 192000U) return 4U;
    if (sample_rate > 72000U && sample_rate <= 96000U) return 3U;
    if (sample_rate > 48000U && sample_rate <= 72000U) return 5U;
    if (sample_rate >= 32000U && sample_rate <= 48000U) return 2U;
    if (sample_rate >= 16000U && sample_rate <= 24000U) return 1U;
    if (sample_rate >= 8000U && sample_rate <= 12000U) return 0U;
    return SSM3582_DAC_FS_NONE;
}

bool ssm3582_bclk_rate_ok(uint32_t sample_rate, uint32_t ratio)
{
    /* The ratios the chip names at all, and Table 13: the span of them
     * each sample-rate band takes. Both endpoints are values from the
     * first list. */
    static const uint16_t s_ratios[] = {50U,    64U,   100U, 128U, 192U,   200U,   256U,
                                        384U,   512U,  768U, 800U, 1024U,  2048U,  4096U};
    static const struct {
        uint32_t lo_hz;
        uint32_t hi_hz;
        uint16_t lo_ratio;
        uint16_t hi_ratio;
    } s_bands[] = {
        {8000U, 12000U, 200U, 4096U},
        {16000U, 24000U, 100U, 1024U},
        {32000U, 48000U, 50U, 1024U},
        {64000U, 96000U, 50U, 512U},
        {128000U, 192000U, 50U, 256U},
    };
    /* The clock itself, at the bottom for its internals to work and at
     * the top for the pins. */
    const uint64_t bclk_hz = (uint64_t)sample_rate * ratio;
    if (bclk_hz < 2048000ULL || bclk_hz > 49152000ULL) return false;

    bool listed = false;
    for (size_t index = 0U; index < sizeof(s_ratios) / sizeof(s_ratios[0]); ++index) {
        if (s_ratios[index] == (uint16_t)ratio) {
            listed = true;
            break;
        }
    }
    if (!listed) return false;

    for (size_t index = 0U; index < sizeof(s_bands) / sizeof(s_bands[0]); ++index) {
        if (sample_rate >= s_bands[index].lo_hz && sample_rate <= s_bands[index].hi_hz) {
            return ratio >= s_bands[index].lo_ratio && ratio <= s_bands[index].hi_ratio;
        }
    }
    /* Between the bands - 13 kHz to 15.9 kHz - the chip has no settings
     * at all, whatever the clock. */
    return false;
}