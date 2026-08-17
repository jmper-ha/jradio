#include <assert.h>
#include <stdio.h>

#include "ui_vu_meter.h"

static void test_the_scale_spans_silence_to_full(void)
{
    assert(ui_vu_percent_from_level(0U) == 0U);
    assert(ui_vu_percent_from_level(UI_VU_FULL_SCALE) == 100U);
    /* Anything the DAC can produce is on the scale, including the value a
     * saturated negative sample folds to. */
    assert(ui_vu_percent_from_level(65535U) == 100U);
}

static void test_the_scale_is_logarithmic_not_linear(void)
{
    /* Half of full scale is -6 dB. As an RMS reading that is a denser signal
     * than music sustains, so it tops the scale out; a linear bar would call
     * it exactly half.  */
    assert(ui_vu_percent_from_level(UI_VU_FULL_SCALE / 2U) >= 95U);

    /* A quarter (-12 dB) is where loud music actually reads, and it has to sit
     * high with room left in both directions. */
    const uint8_t quarter = ui_vu_percent_from_level(UI_VU_FULL_SCALE / 4U);
    assert(quarter > 65U && quarter < 90U);

    /* An eighth (-18 dB) is a quiet passage, not a dead meter. */
    const uint8_t eighth = ui_vu_percent_from_level(UI_VU_FULL_SCALE / 8U);
    assert(eighth > 45U && eighth < quarter);
}

static void test_the_scale_rises_monotonically(void)
{
    /* A dip anywhere would show as the bar twitching backwards while the
     * music gets louder. */
    uint8_t previous = 0U;
    for (uint32_t level = 0U; level <= UI_VU_FULL_SCALE; level += 97U) {
        const uint8_t percent = ui_vu_percent_from_level((uint16_t)level);
        assert(percent >= previous);
        assert(percent <= 100U);
        previous = percent;
    }
}

static void test_very_quiet_signals_read_as_empty(void)
{
    /* Below the floor the bar must actually empty: a meter that never quite
     * reaches zero looks stuck rather than quiet. */
    assert(ui_vu_percent_from_level(1U) == 0U);
    assert(ui_vu_percent_from_level(60U) == 0U);
    assert(ui_vu_percent_from_level(2000U) > 0U);
}

static void test_the_meter_jumps_straight_up(void)
{
    /* A transient lasting one block still has to reach its real height. */
    ui_vu_meter_t meter;
    ui_vu_meter_init(&meter);
    assert(ui_vu_meter_step(&meter, 90U, 10U) == 90U);
    assert(ui_vu_meter_step(&meter, 100U, 0U) == 100U);
}

static void test_the_meter_falls_gradually(void)
{
    ui_vu_meter_t meter;
    ui_vu_meter_init(&meter);
    (void)ui_vu_meter_step(&meter, 100U, 10U);

    /* Silence arrives; one 100 ms tick must not empty the bar. */
    const uint8_t after = ui_vu_meter_step(&meter, 0U, 100U);
    assert(after < 100U && after > 80U);

    /* But it does empty within a couple of seconds. */
    for (int tick = 0; tick < 40; ++tick) {
        (void)ui_vu_meter_step(&meter, 0U, 50U);
    }
    assert(ui_vu_meter_step(&meter, 0U, 50U) == 0U);
}

static void test_the_fall_never_undershoots_the_target(void)
{
    /* Falling towards a level that is not zero must stop there, not sail
     * past it into a dip the signal never had. */
    ui_vu_meter_t meter;
    ui_vu_meter_init(&meter);
    (void)ui_vu_meter_step(&meter, 100U, 10U);
    const uint8_t held = ui_vu_meter_step(&meter, 60U, 10000U);
    assert(held == 60U);
}

static void test_a_long_gap_does_not_wrap_the_bar(void)
{
    /* The UI can be away for a while - another screen, a slow flush - and the
     * elapsed time then dwarfs the level. Unsigned subtraction would wrap and
     * paint a full bar out of silence. */
    ui_vu_meter_t meter;
    ui_vu_meter_init(&meter);
    (void)ui_vu_meter_step(&meter, 40U, 10U);
    assert(ui_vu_meter_step(&meter, 0U, 60000U) == 0U);
}

static void test_a_target_above_the_scale_is_clamped(void)
{
    ui_vu_meter_t meter;
    ui_vu_meter_init(&meter);
    assert(ui_vu_meter_step(&meter, 200U, 10U) == 100U);
}

static void test_the_meter_tolerates_being_handed_nothing(void)
{
    ui_vu_meter_init(NULL);
    assert(ui_vu_meter_step(NULL, 50U, 10U) == 0U);
}


static void test_segment_count_is_exact_at_both_ends(void)
{
    /* Silence must light nothing and full scale everything - a meter that
     * keeps one segment lit in silence, or never fills, reads as broken. */
    assert(ui_vu_lit_segments(0U, 20U) == 0U);
    assert(ui_vu_lit_segments(100U, 20U) == 20U);
    assert(ui_vu_lit_segments(120U, 20U) == 20U);
}

static void test_segment_count_rounds_to_nearest(void)
{
    assert(ui_vu_lit_segments(50U, 20U) == 10U);
    assert(ui_vu_lit_segments(52U, 20U) == 10U);
    assert(ui_vu_lit_segments(58U, 20U) == 12U);
}

static void test_a_faint_signal_still_lights_one_segment(void)
{
    /* Rounding alone would floor a quiet passage to zero and make the meter
     * look dead while sound is playing. */
    assert(ui_vu_lit_segments(1U, 20U) == 1U);
    assert(ui_vu_lit_segments(2U, 20U) == 1U);
}

static void test_segment_count_never_exceeds_the_bar(void)
{
    for (uint8_t percent = 0U; percent <= 100U; ++percent) {
        assert(ui_vu_lit_segments(percent, 20U) <= 20U);
    }
    assert(ui_vu_lit_segments(50U, 0U) == 0U);
}

static void test_the_red_zone_sits_at_the_top(void)
{
    assert(!ui_vu_segment_is_red(0U, 20U));
    assert(!ui_vu_segment_is_red(15U, 20U));
    assert(ui_vu_segment_is_red(16U, 20U));
    assert(ui_vu_segment_is_red(19U, 20U));
    assert(!ui_vu_segment_is_red(0U, 0U));
}

int main(void)
{
    test_the_scale_spans_silence_to_full();
    test_the_scale_is_logarithmic_not_linear();
    test_the_scale_rises_monotonically();
    test_very_quiet_signals_read_as_empty();
    test_the_meter_jumps_straight_up();
    test_the_meter_falls_gradually();
    test_the_fall_never_undershoots_the_target();
    test_a_long_gap_does_not_wrap_the_bar();
    test_a_target_above_the_scale_is_clamped();
    test_the_meter_tolerates_being_handed_nothing();
    test_segment_count_is_exact_at_both_ends();
    test_segment_count_rounds_to_nearest();
    test_a_faint_signal_still_lights_one_segment();
    test_segment_count_never_exceeds_the_bar();
    test_the_red_zone_sits_at_the_top();
    puts("ui_vu_meter tests passed");
    return 0;
}
