#include "ui_vu_meter.h"

#include <stddef.h>

/* Breakpoints of the dB curve: RMS magnitude and the bar percentage it maps
 * to, from the top of the scale down to the noise floor. Interpolated linearly
 * between neighbours, which is close enough at this size - the bar is a couple
 * of hundred pixels and nobody reads a VU meter to three decimals. Integer-only
 * on purpose: no libm on the device, no -lm in the host tests.
 *
 * The scale spans 20 dB, from -5 dBFS at the top to -25 dBFS at the bottom,
 * and that width is the whole point. Level readings measured on this device
 * with real music averaged about 7400 out of a full scale of 32768 and moved
 * some 14% block to block. Spread over the 48 dB the scale used to cover, that
 * movement was under four percent of the bar per block and the whole of the
 * music sat in the top third - which is what "sluggish" looks like. Over 20 dB
 * the same music travels thirteen blocks of twenty and moves about one block
 * per reading, twenty-three milliseconds apart.
 *
 * The position also follows measurement: readings rose 1.7 dB when the meter
 * moved from averaging a whole decoded block to 5.8 ms windows, so the scale
 * moved up with them rather than letting the bar peg at the top.
 *
 * The cost is deliberate: anything under -25 dBFS reads empty. That is a
 * genuinely quiet passage, and a meter that still shows something there is
 * spending its resolution where nothing is happening.
 *
 * RMS and not peak: mastered music holds its peaks within a couple of dB of
 * full scale almost continuously, and a peak-fed bar sat pinned at the top. */
typedef struct {
    uint16_t level;
    uint8_t percent;
} ui_vu_point_t;

/* Even 2 dB per ten percent, so the bar is linear in decibels across its
 * whole length rather than only near the top. */
static const ui_vu_point_t s_curve[] = {
    {18426U, 100U},  /*  -5 dBFS */
    {14640U, 90U},   /*  -7 dBFS */
    {11626U, 80U},   /*  -9 dBFS */
    {9238U, 70U},    /* -11 dBFS */
    {7338U, 60U},    /* -13 dBFS */
    {5828U, 50U},    /* -15 dBFS */
    {4630U, 40U},    /* -17 dBFS */
    {3677U, 30U},    /* -19 dBFS */
    {2921U, 20U},    /* -21 dBFS */
    {2320U, 10U},    /* -23 dBFS */
    {1843U, 0U},     /* -25 dBFS */
};

void ui_vu_meter_init(ui_vu_meter_t *meter)
{
    if (meter != NULL) meter->percent = 0U;
}

uint8_t ui_vu_percent_from_level(uint16_t level)
{
    const size_t count = sizeof(s_curve) / sizeof(s_curve[0]);
    if (level >= s_curve[0].level) return s_curve[0].percent;
    if (level <= s_curve[count - 1U].level) return 0U;

    for (size_t index = 1U; index < count; ++index) {
        if (level > s_curve[index].level) {
            /* Between index-1 (louder) and index (quieter). */
            const ui_vu_point_t high = s_curve[index - 1U];
            const ui_vu_point_t low = s_curve[index];
            const uint32_t span = (uint32_t)(high.level - low.level);
            const uint32_t into = (uint32_t)(level - low.level);
            const uint32_t range = (uint32_t)(high.percent - low.percent);
            return (uint8_t)(low.percent + (into * range) / span);
        }
    }
    return 0U;
}

uint8_t ui_vu_meter_step(ui_vu_meter_t *meter, uint8_t target_percent,
                         uint32_t elapsed_ms)
{
    if (meter == NULL) return 0U;
    if (target_percent > 100U) target_percent = 100U;
    /* Attack is instant: a transient that only lasted one block still has to
     * reach its true height, or the meter under-reads exactly the peaks it
     * exists to show. */
    if (target_percent >= meter->percent) {
        meter->percent = target_percent;
        return meter->percent;
    }
    const uint32_t fall = (elapsed_ms * UI_VU_RELEASE_PERCENT_PER_SEC) / 1000U;
    meter->percent = fall >= meter->percent ? target_percent
                   : (uint8_t)(meter->percent - fall) < target_percent
                         ? target_percent
                         : (uint8_t)(meter->percent - fall);
    return meter->percent;
}

uint8_t ui_vu_lit_segments(uint8_t percent, uint8_t count)
{
    if (count == 0U) return 0U;
    if (percent == 0U) return 0U;
    if (percent >= 100U) return count;
    const uint32_t lit = ((uint32_t)percent * count + 50U) / 100U;
    if (lit == 0U) return 1U;  /* Audible but faint still shows something. */
    return (uint8_t)(lit > count ? count : lit);
}

bool ui_vu_segment_is_red(uint8_t index, uint8_t count)
{
    /* Top fifth. Roughly the last 6 dB of the scale, which is where a signal
     * is close enough to clipping to be worth flagging. */
    if (count == 0U) return false;
    return index >= (uint8_t)((uint32_t)count * 4U / 5U);
}
