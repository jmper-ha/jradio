#include "ui_busy_bar.h"

ui_busy_bar_span_t ui_busy_bar_span(uint32_t now_ms, uint32_t started_ms)
{
    /* Unsigned difference, so a wait that straddles the tick counter's wrap
     * carries on sweeping instead of jumping to the far end for 49 days. */
    const uint32_t elapsed = now_ms - started_ms;
    const uint32_t phase = elapsed % (2U * UI_BUSY_BAR_SWEEP_MS);
    const uint32_t travel = 100U - UI_BUSY_BAR_SEGMENT;
    /* Out on the first half of the period and back on the second. The two
     * meet at exactly `travel` where they change over, so the turn is smooth
     * rather than a jump of one step. */
    const uint32_t start = phase < UI_BUSY_BAR_SWEEP_MS
                               ? travel * phase / UI_BUSY_BAR_SWEEP_MS
                               : travel * (2U * UI_BUSY_BAR_SWEEP_MS - phase) /
                                     UI_BUSY_BAR_SWEEP_MS;
    const ui_busy_bar_span_t span = {
        .start = (uint8_t)start,
        .end = (uint8_t)(start + UI_BUSY_BAR_SEGMENT),
    };
    return span;
}
