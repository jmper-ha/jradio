#include <assert.h>
#include <stdio.h>

#include "ui_fm_tune.h"

static void test_the_knob_moves_in_tenths_within_the_band(void)
{
    ui_fm_tune_t tune;
    ui_fm_tune_reset(&tune);
    assert(!ui_fm_tune_is_active(&tune));
    assert(!ui_fm_tune_begin(&tune, 0U, 0U));
    assert(ui_fm_tune_begin(&tune, 101200U, 0U));
    assert(ui_fm_tune_move(&tune, 1, 10U));
    assert(ui_fm_tune_khz(&tune) == 101300U);
    assert(ui_fm_tune_move(&tune, -1, 20U));
    assert(ui_fm_tune_move(&tune, -1, 30U));
    assert(ui_fm_tune_khz(&tune) == 101100U);

    // The ends hold, and say so.
    assert(ui_fm_tune_begin(&tune, 108000U, 0U));
    assert(!ui_fm_tune_move(&tune, 1, 10U));
    assert(ui_fm_tune_khz(&tune) == 108000U);
    assert(ui_fm_tune_begin(&tune, 87000U, 0U));
    assert(!ui_fm_tune_move(&tune, -1, 10U));
    assert(ui_fm_tune_khz(&tune) == 87000U);
}

static void test_a_fast_turn_sends_the_latest_at_a_measured_pace(void)
{
    ui_fm_tune_t tune;
    ui_fm_tune_begin(&tune, 101200U, 0U);
    uint32_t khz = 0U;
    assert(!ui_fm_tune_due(&tune, 100U, &khz));  // nothing moved yet
    for (int click = 0; click < 5; ++click) ui_fm_tune_move(&tune, 1, 100U);
    assert(ui_fm_tune_due(&tune, 100U, &khz) && khz == 101700U);
    ui_fm_tune_move(&tune, 1, 120U);
    assert(!ui_fm_tune_due(&tune, 150U, &khz));  // too soon after the last
    assert(ui_fm_tune_due(&tune, 180U, &khz) && khz == 101800U);
    assert(!ui_fm_tune_due(&tune, 400U, &khz));  // and nothing new since
}

static void test_a_press_keeps_and_anything_else_goes_back(void)
{
    ui_fm_tune_t tune;
    ui_fm_tune_begin(&tune, 101200U, 0U);
    ui_fm_tune_move(&tune, 1, 10U);
    ui_fm_tune_move(&tune, 1, 20U);
    assert(ui_fm_tune_keep(&tune) == 101400U);
    assert(!ui_fm_tune_is_active(&tune));

    ui_fm_tune_begin(&tune, 101200U, 0U);
    ui_fm_tune_move(&tune, 1, 10U);
    assert(ui_fm_tune_cancel(&tune) == 101200U);
    assert(!ui_fm_tune_is_active(&tune));
    // Closed, it answers nothing.
    assert(ui_fm_tune_keep(&tune) == 0U && ui_fm_tune_cancel(&tune) == 0U);
}

static void test_a_seek_from_inside_is_followed(void)
{
    ui_fm_tune_t tune;
    ui_fm_tune_begin(&tune, 101200U, 0U);
    ui_fm_tune_follow(&tune, 103700U, 500U);
    assert(ui_fm_tune_khz(&tune) == 103700U);
    uint32_t khz = 0U;
    assert(!ui_fm_tune_due(&tune, 1000U, &khz));  // the tuner is already there
    ui_fm_tune_move(&tune, 1, 1000U);
    assert(ui_fm_tune_due(&tune, 1000U, &khz) && khz == 103800U);
    // And cancelling still goes back to where the mode began.
    assert(ui_fm_tune_cancel(&tune) == 101200U);
}

static void test_it_closes_after_a_quiet_spell(void)
{
    ui_fm_tune_t tune;
    ui_fm_tune_begin(&tune, 101200U, 1000U);
    assert(!ui_fm_tune_idle(&tune, 1000U + UI_FM_TUNE_IDLE_MS - 1U));
    ui_fm_tune_move(&tune, 1, 5000U);
    assert(!ui_fm_tune_idle(&tune, 1000U + UI_FM_TUNE_IDLE_MS));
    assert(ui_fm_tune_idle(&tune, 5000U + UI_FM_TUNE_IDLE_MS));
}

int main(void)
{
    test_the_knob_moves_in_tenths_within_the_band();
    test_a_fast_turn_sends_the_latest_at_a_measured_pace();
    test_a_press_keeps_and_anything_else_goes_back();
    test_a_seek_from_inside_is_followed();
    test_it_closes_after_a_quiet_spell();
    puts("ui_fm_tune tests passed");
    return 0;
}
