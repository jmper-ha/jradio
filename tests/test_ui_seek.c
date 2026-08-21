#include <assert.h>
#include <stdio.h>

#include "ui_seek.h"

static void test_the_mode_opens_at_the_position_playing_now(void)
{
    ui_seek_t seek;
    ui_seek_reset(&seek);
    assert(!ui_seek_is_active(&seek));

    assert(ui_seek_begin(&seek, 42U, 200U));
    assert(ui_seek_is_active(&seek));
    assert(ui_seek_target(&seek) == 42U);
    assert(ui_seek_percent(&seek) == 21U);
}

static void test_a_track_of_no_known_length_cannot_be_scrubbed(void)
{
    /* The length is estimated from the first frame's bitrate, so it is 0 for
     * the first moments of every track and for the whole of one whose bitrate
     * never became known. There is no scale to aim along, and no bar on the
     * screen either. */
    ui_seek_t seek;
    ui_seek_reset(&seek);
    assert(!ui_seek_begin(&seek, 10U, 0U));
    assert(!ui_seek_is_active(&seek));
    assert(!ui_seek_begin(NULL, 10U, 200U));
}

static void test_a_position_past_the_estimate_starts_at_the_end(void)
{
    /* A variable-bitrate file routinely outlives the length computed from its
     * average bitrate, so the position can already be off the end of the scale
     * when the mode opens. */
    ui_seek_t seek;
    ui_seek_reset(&seek);
    assert(ui_seek_begin(&seek, 260U, 240U));
    assert(ui_seek_target(&seek) == 240U);
    assert(ui_seek_percent(&seek) == 100U);
}

static void test_the_step_scales_with_the_track_but_stays_in_reach(void)
{
    /* A song gets the smallest useful step; a concert recording gets one that
     * crosses it in a minute of turning rather than ten. */
    assert(ui_seek_step_seconds(0U) == UI_SEEK_STEP_MIN_S);
    assert(ui_seek_step_seconds(180U) == UI_SEEK_STEP_MIN_S);
    /* 300 s is where a sixtieth first exceeds the floor. */
    assert(ui_seek_step_seconds(300U) == UI_SEEK_STEP_MIN_S);
    assert(ui_seek_step_seconds(600U) == 10U);
    assert(ui_seek_step_seconds(3600U) == UI_SEEK_STEP_MAX_S);
    /* And no further: a two-hour file would otherwise step by two minutes. */
    assert(ui_seek_step_seconds(7200U) == UI_SEEK_STEP_MAX_S);
}

static void test_the_knob_moves_the_target_and_stops_at_both_ends(void)
{
    ui_seek_t seek;
    ui_seek_reset(&seek);
    assert(ui_seek_begin(&seek, 100U, 180U));
    assert(ui_seek_move(&seek, 1));
    assert(ui_seek_target(&seek) == 105U);
    assert(ui_seek_move(&seek, -1));
    assert(ui_seek_target(&seek) == 100U);

    /* Forwards to the very end, then no further - and the refusal is reported,
     * so a turn against the stop does not redraw the screen. */
    for (int click = 0; click < 100; ++click) {
        (void)ui_seek_move(&seek, 1);
    }
    assert(ui_seek_target(&seek) == 180U);
    assert(ui_seek_percent(&seek) == 100U);
    assert(!ui_seek_move(&seek, 1));

    /* The last step lands exactly on the end rather than overshooting it: 180
     * is not a multiple of the 5 s step from 100. */
    ui_seek_reset(&seek);
    assert(ui_seek_begin(&seek, 178U, 180U));
    assert(ui_seek_move(&seek, 1));
    assert(ui_seek_target(&seek) == 180U);

    /* And backwards to zero, which is a real position, not a stop. */
    ui_seek_reset(&seek);
    assert(ui_seek_begin(&seek, 3U, 180U));
    assert(ui_seek_move(&seek, -1));
    assert(ui_seek_target(&seek) == 0U);
    assert(ui_seek_percent(&seek) == 0U);
    assert(!ui_seek_move(&seek, -1));
}

static void test_nothing_moves_while_the_mode_is_closed(void)
{
    /* The encoder belongs to the volume then, and a stale target would jump
     * the file the next time the mode opened. */
    ui_seek_t seek;
    ui_seek_reset(&seek);
    assert(!ui_seek_move(&seek, 1));
    assert(ui_seek_target(&seek) == 0U);

    assert(ui_seek_begin(&seek, 50U, 200U));
    ui_seek_reset(&seek);
    assert(!ui_seek_is_active(&seek));
    assert(!ui_seek_move(&seek, -1));
}

static void test_the_model_tolerates_being_handed_nothing(void)
{
    ui_seek_reset(NULL);
    assert(!ui_seek_is_active(NULL));
    assert(!ui_seek_move(NULL, 1));
    assert(ui_seek_target(NULL) == 0U);
    assert(ui_seek_percent(NULL) == 0U);
}

int main(void)
{
    test_the_mode_opens_at_the_position_playing_now();
    test_a_track_of_no_known_length_cannot_be_scrubbed();
    test_a_position_past_the_estimate_starts_at_the_end();
    test_the_step_scales_with_the_track_but_stays_in_reach();
    test_the_knob_moves_the_target_and_stops_at_both_ends();
    test_nothing_moves_while_the_mode_is_closed();
    test_the_model_tolerates_being_handed_nothing();
    puts("ui_seek tests passed");
    return 0;
}
