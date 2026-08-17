#include "ui_vu_meter.h"

#include <stddef.h>

/* Breakpoints of the dB curve: RMS magnitude and the bar percentage it maps
 * to, from the top of the scale down to the noise floor. Interpolated linearly
 * between neighbours, which is close enough at this size - the bar is a couple
 * of hundred pixels and nobody reads a VU meter to three decimals. Integer-only
 * on purpose: no libm on the device, no -lm in the host tests.
 *
 * The top of the scale is -6 dBFS, not 0. RMS only reaches full scale for a
 * square wave; music short-term RMS runs 10-14 dB under its own peaks, so a
 * scale topping out at 0 dBFS would leave the last quarter of the bar
 * permanently dark. -6 dB puts loud material around three quarters up with
 * room to move in both directions. */
typedef struct {
    uint16_t level;
    uint8_t percent;
} ui_vu_point_t;

static const ui_vu_point_t s_curve[] = {
    {16422U, 100U},  /*  -6 dBFS */
    {11627U, 90U},   /*  -9 dBFS */
    {8218U, 80U},    /* -12 dBFS */
    {4125U, 60U},    /* -18 dBFS */
    {2067U, 45U},    /* -24 dBFS */
    {1036U, 30U},    /* -30 dBFS */
    {519U, 20U},     /* -36 dBFS */
    {164U, 8U},      /* -46 dBFS */
    {65U, 0U},       /* -54 dBFS */
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
