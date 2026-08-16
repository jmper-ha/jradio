#include "ui_vu_meter.h"

#include <stddef.h>

/* Breakpoints of the dB curve: peak magnitude and the bar percentage it maps
 * to, from full scale down to the noise floor. Interpolated linearly between
 * neighbours, which is close enough at this size - the bar is a couple of
 * hundred pixels and nobody reads a VU meter to three decimals. Integer-only
 * on purpose: no libm on the device, no -lm in the host tests. */
typedef struct {
    uint16_t peak;
    uint8_t percent;
} ui_vu_point_t;

static const ui_vu_point_t s_curve[] = {
    {32768U, 100U},  /*   0 dB */
    {23197U, 90U},   /*  -3 dB */
    {16422U, 80U},   /*  -6 dB */
    {8218U, 60U},    /* -12 dB */
    {4125U, 45U},    /* -18 dB */
    {2067U, 30U},    /* -24 dB */
    {1036U, 20U},    /* -30 dB */
    {328U, 8U},      /* -40 dB */
    {130U, 0U},      /* -48 dB */
};

void ui_vu_meter_init(ui_vu_meter_t *meter)
{
    if (meter != NULL) meter->percent = 0U;
}

uint8_t ui_vu_percent_from_peak(uint16_t peak)
{
    const size_t count = sizeof(s_curve) / sizeof(s_curve[0]);
    if (peak >= s_curve[0].peak) return s_curve[0].percent;
    if (peak <= s_curve[count - 1U].peak) return 0U;

    for (size_t index = 1U; index < count; ++index) {
        if (peak > s_curve[index].peak) {
            /* Between index-1 (louder) and index (quieter). */
            const ui_vu_point_t high = s_curve[index - 1U];
            const ui_vu_point_t low = s_curve[index];
            const uint32_t span = (uint32_t)(high.peak - low.peak);
            const uint32_t into = (uint32_t)(peak - low.peak);
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
