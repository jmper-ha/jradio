#include <assert.h>
#include <stdio.h>

#include "ui_busy_bar.h"

/* The bar is the only thing on the screen saying the device has not hung, so
   what is tested here is that it never stops moving and never leaves the bar -
   including at the moment the tick counter wraps, which is the one case that
   cannot be checked by looking at the panel. */

static void test_the_segment_keeps_its_width_and_stays_on_the_bar(void)
{
    for (uint32_t elapsed = 0U; elapsed < 8U * UI_BUSY_BAR_SWEEP_MS; elapsed += 7U) {
        const ui_busy_bar_span_t span = ui_busy_bar_span(elapsed, 0U);
        assert(span.end - span.start == (int)UI_BUSY_BAR_SEGMENT);
        assert(span.end <= 100U);
    }
}

static void test_it_travels_out_and_comes_back(void)
{
    const ui_busy_bar_span_t begin = ui_busy_bar_span(0U, 0U);
    const ui_busy_bar_span_t middle = ui_busy_bar_span(UI_BUSY_BAR_SWEEP_MS, 0U);
    const ui_busy_bar_span_t back = ui_busy_bar_span(2U * UI_BUSY_BAR_SWEEP_MS, 0U);

    assert(begin.start == 0U);
    assert(middle.end == 100U);
    assert(back.start == 0U);

    /* Monotonic on the way out, which is what stops the segment jittering back
       and forth within one pass. */
    uint8_t previous = 0U;
    for (uint32_t elapsed = 0U; elapsed <= UI_BUSY_BAR_SWEEP_MS; elapsed += 10U) {
        const ui_busy_bar_span_t span = ui_busy_bar_span(elapsed, 0U);
        assert(span.start >= previous);
        previous = span.start;
    }

    /* And it does move: a tenth of a second apart is two different places, or
       the indicator says nothing at all. */
    assert(ui_busy_bar_span(0U, 0U).start != ui_busy_bar_span(100U, 0U).start);
}

static void test_a_wait_that_crosses_the_tick_wrap_carries_on(void)
{
    /* Started 100 ms before the counter wrapped, read 100 ms after it. The
       elapsed time is 200 ms either way. */
    const uint32_t started = 0xFFFFFF9CU;   /* -100 */
    const ui_busy_bar_span_t across = ui_busy_bar_span(100U, started);
    const ui_busy_bar_span_t plain = ui_busy_bar_span(200U, 0U);
    assert(across.start == plain.start);
    assert(across.end == plain.end);
}

int main(void)
{
    test_the_segment_keeps_its_width_and_stays_on_the_bar();
    test_it_travels_out_and_comes_back();
    test_a_wait_that_crosses_the_tick_wrap_carries_on();
    printf("ui_busy_bar tests passed\n");
    return 0;
}
