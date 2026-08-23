#include "ui_text_scroll.h"

#include <assert.h>
#include <stdio.h>

/* 320 px of text in a 200 px box: 120 px of overhang, which at 40 px/s is a
 * 3000 ms traversal - comfortably inside the clamps, so the arithmetic here is
 * the proportional one rather than a clamped constant. */
#define TEXT_W 320
#define VIEW_W 200
#define OVERHANG (TEXT_W - VIEW_W)
#define TRAVEL 3000U

static void test_text_that_fits_never_moves(void)
{
    /* The caller applies the offset blindly, so "fits" has to answer zero at
     * every point of the cycle rather than being a question the caller asks
     * first. */
    for (uint32_t t = 0U; t < 20000U; t += 137U) {
        assert(ui_text_scroll_offset(UI_TEXT_SCROLL_BOUNCE, 100, 200, t) == 0);
        assert(ui_text_scroll_offset(UI_TEXT_SCROLL_LEFT, 100, 200, t) == 0);
        /* Exactly filling the box is still fitting: an overhang of zero has
         * nowhere to travel and would divide by a zero cycle. */
        assert(ui_text_scroll_offset(UI_TEXT_SCROLL_LEFT, 200, 200, t) == 0);
    }
    assert(ui_text_scroll_cycle_ms(UI_TEXT_SCROLL_LEFT, 100, 200) == 0U);
}

static void test_bounce_goes_out_holds_comes_back_and_pauses(void)
{
    const uint32_t cycle = ui_text_scroll_cycle_ms(UI_TEXT_SCROLL_BOUNCE, TEXT_W, VIEW_W);
    assert(cycle == TRAVEL + UI_TEXT_SCROLL_BOUNCE_HOLD_MS + TRAVEL +
                        UI_TEXT_SCROLL_REPEAT_MS);

    assert(ui_text_scroll_offset(UI_TEXT_SCROLL_BOUNCE, TEXT_W, VIEW_W, 0U) == 0);
    /* Halfway out. */
    assert(ui_text_scroll_offset(UI_TEXT_SCROLL_BOUNCE, TEXT_W, VIEW_W, TRAVEL / 2U) ==
           -OVERHANG / 2);
    /* At the far end, and staying there through the hold. */
    assert(ui_text_scroll_offset(UI_TEXT_SCROLL_BOUNCE, TEXT_W, VIEW_W, TRAVEL) == -OVERHANG);
    assert(ui_text_scroll_offset(UI_TEXT_SCROLL_BOUNCE, TEXT_W, VIEW_W,
                                 TRAVEL + UI_TEXT_SCROLL_BOUNCE_HOLD_MS - 1U) == -OVERHANG);

    /* Coming back: halfway through the return trip is halfway back. This is
     * the whole difference from "left" mode - the text is visibly travelling
     * rather than already home. */
    const uint32_t back = TRAVEL + UI_TEXT_SCROLL_BOUNCE_HOLD_MS;
    assert(ui_text_scroll_offset(UI_TEXT_SCROLL_BOUNCE, TEXT_W, VIEW_W, back + TRAVEL / 2U) ==
           -OVERHANG / 2);
    assert(ui_text_scroll_offset(UI_TEXT_SCROLL_BOUNCE, TEXT_W, VIEW_W, back + TRAVEL) == 0);

    /* Then the three-second pause on the start, right up to the wrap. */
    assert(ui_text_scroll_offset(UI_TEXT_SCROLL_BOUNCE, TEXT_W, VIEW_W, cycle - 1U) == 0);
    assert(ui_text_scroll_offset(UI_TEXT_SCROLL_BOUNCE, TEXT_W, VIEW_W, cycle) == 0);
}

static void test_left_snaps_back_instead_of_travelling(void)
{
    const uint32_t cycle = ui_text_scroll_cycle_ms(UI_TEXT_SCROLL_LEFT, TEXT_W, VIEW_W);
    assert(cycle == TRAVEL + UI_TEXT_SCROLL_LEFT_HOLD_MS + UI_TEXT_SCROLL_REPEAT_MS);

    assert(ui_text_scroll_offset(UI_TEXT_SCROLL_LEFT, TEXT_W, VIEW_W, 0U) == 0);
    assert(ui_text_scroll_offset(UI_TEXT_SCROLL_LEFT, TEXT_W, VIEW_W, TRAVEL / 2U) ==
           -OVERHANG / 2);
    /* The tail is held for a full second - the reader's only chance at it,
     * since it does not come back past the eye the way the bounce does. */
    assert(ui_text_scroll_offset(UI_TEXT_SCROLL_LEFT, TEXT_W, VIEW_W, TRAVEL) == -OVERHANG);
    assert(ui_text_scroll_offset(UI_TEXT_SCROLL_LEFT, TEXT_W, VIEW_W,
                                 TRAVEL + UI_TEXT_SCROLL_LEFT_HOLD_MS - 1U) == -OVERHANG);

    /* And then it is simply home: the very next millisecond, with nothing
     * drawn in between. */
    assert(ui_text_scroll_offset(UI_TEXT_SCROLL_LEFT, TEXT_W, VIEW_W,
                                 TRAVEL + UI_TEXT_SCROLL_LEFT_HOLD_MS) == 0);
    assert(ui_text_scroll_offset(UI_TEXT_SCROLL_LEFT, TEXT_W, VIEW_W, cycle - 1U) == 0);

    /* Never seen travelling rightwards, which is the property that separates
     * the two modes: sampled across a whole cycle, the offset only ever
     * decreases or jumps home. */
    int previous = 0;
    for (uint32_t t = 1U; t < cycle; t += 7U) {
        const int now = ui_text_scroll_offset(UI_TEXT_SCROLL_LEFT, TEXT_W, VIEW_W, t);
        assert(now <= previous || now == 0);
        previous = now;
    }
}

static void test_the_offset_stays_inside_the_overhang(void)
{
    /* The offset is applied to a real object, so an out-of-range value would
     * park the text off screen for good rather than glitch for a frame. */
    const int widths[] = {201, 210, 320, 1000, 20000};
    for (size_t index = 0U; index < sizeof(widths) / sizeof(widths[0]); ++index) {
        const int overhang = widths[index] - VIEW_W;
        for (uint32_t t = 0U; t < 40000U; t += 53U) {
            for (int mode = 0; mode <= 1; ++mode) {
                const int offset = ui_text_scroll_offset((ui_text_scroll_mode_t)mode,
                                                         widths[index], VIEW_W, t);
                assert(offset <= 0 && offset >= -overhang);
            }
        }
    }
}

static void test_travel_time_is_clamped_at_both_ends(void)
{
    /* A one-pixel overhang would otherwise flick past in 25 ms, and a very
     * long line would crawl for minutes. */
    const uint32_t tiny = ui_text_scroll_cycle_ms(UI_TEXT_SCROLL_LEFT, VIEW_W + 1, VIEW_W);
    assert(tiny == UI_TEXT_SCROLL_TRAVEL_MIN_MS + UI_TEXT_SCROLL_LEFT_HOLD_MS +
                       UI_TEXT_SCROLL_REPEAT_MS);

    const uint32_t huge = ui_text_scroll_cycle_ms(UI_TEXT_SCROLL_LEFT, VIEW_W + 100000, VIEW_W);
    assert(huge == UI_TEXT_SCROLL_TRAVEL_MAX_MS + UI_TEXT_SCROLL_LEFT_HOLD_MS +
                       UI_TEXT_SCROLL_REPEAT_MS);
}

static void test_a_missed_frame_catches_up_rather_than_drifting(void)
{
    /* The offset is a function of the clock, so a caller that skipped a
     * second's worth of frames lands where the animation should be - not a
     * second behind it. Two cycles on is the same place. */
    const uint32_t cycle = ui_text_scroll_cycle_ms(UI_TEXT_SCROLL_BOUNCE, TEXT_W, VIEW_W);
    for (uint32_t t = 0U; t < cycle; t += 211U) {
        const int here = ui_text_scroll_offset(UI_TEXT_SCROLL_BOUNCE, TEXT_W, VIEW_W, t);
        assert(ui_text_scroll_offset(UI_TEXT_SCROLL_BOUNCE, TEXT_W, VIEW_W, t + cycle) == here);
        assert(ui_text_scroll_offset(UI_TEXT_SCROLL_BOUNCE, TEXT_W, VIEW_W, t + 2U * cycle) ==
               here);
    }
}

int main(void)
{
    test_text_that_fits_never_moves();
    test_bounce_goes_out_holds_comes_back_and_pauses();
    test_left_snaps_back_instead_of_travelling();
    test_the_offset_stays_inside_the_overhang();
    test_travel_time_is_clamped_at_both_ends();
    test_a_missed_frame_catches_up_rather_than_drifting();
    puts("ui_text_scroll tests passed");
    return 0;
}
