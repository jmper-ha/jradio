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
    /* Half of full scale is -6 dB. As an RMS reading that is denser than music
     * sustains, so it belongs at the very top of the bar - a linear bar would
     * call it exactly half. Not pegged, though: the scale runs to -5. */
    const uint8_t half = ui_vu_percent_from_level(UI_VU_FULL_SCALE / 2U);
    assert(half > 90U && half < 100U);

    /* A quarter (-12 dB) is where loud music reads, and it has to sit above
     * the middle with room left in both directions. */
    const uint8_t quarter = ui_vu_percent_from_level(UI_VU_FULL_SCALE / 4U);
    assert(quarter > 55U && quarter < 80U);

    /* An eighth (-18 dB) is a quiet passage, not a dead meter. Six decibels
     * below the quarter, and the scale is linear in decibels, so it has to
     * land three tenths of the bar lower. */
    const uint8_t eighth = ui_vu_percent_from_level(UI_VU_FULL_SCALE / 8U);
    assert(eighth > 25U && eighth < quarter);
    assert((unsigned int)(quarter - eighth) > 25U);
}


static void test_the_scale_is_wide_enough_to_look_alive(void)
{
    /* The complaint this scale exists to answer was a bar that barely moved.
     * These numbers are the level readings measured on the device with real
     * music playing, and they have to cover most of the bar - if a change here
     * squeezes them back into a third of it, the meter is sluggish again. */
    const uint8_t quiet = ui_vu_percent_from_level(2500U);
    const uint8_t loud = ui_vu_percent_from_level(14000U);
    assert(loud > quiet);
    assert((unsigned int)(loud - quiet) > 55U);

    /* The mean reading has to land mid-bar, not against either end: a meter
     * parked at the top has nowhere to go, one parked at the bottom shows
     * nothing. */
    const uint8_t mean = ui_vu_percent_from_level(7400U);
    assert(mean > 40U && mean < 75U);

    /* And the loudest readings must not peg, or the peaks all look alike. */
    assert(ui_vu_percent_from_level(16385U) < 100U);
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

static void test_the_meter_rises_quickly_but_not_in_one_step(void)
{
    /* The whole point of having an attack time: a single loud block must not
     * throw the bar to the top, or the meter flickers too fast to read. It
     * still has to get there promptly - a swell that never registers is no
     * better. */
    ui_vu_meter_t meter;
    ui_vu_meter_init(&meter);

    const uint8_t first = ui_vu_meter_step(&meter, 100U, 40U);
    assert(first > 20U);
    assert(first < 100U);

    /* Visibly there within about a sixth of a second - the remaining few
     * percent close a point at a time and are under a fifth of one block. */
    uint8_t value = first;
    for (int tick = 0; tick < 3; ++tick) {
        value = ui_vu_meter_step(&meter, 100U, 40U);
    }
    assert(value >= 90U);
}

static void test_the_meter_rises_faster_than_it_falls(void)
{
    /* A swell should register at once; a gap should linger long enough to see.
     * Same gap, same elapsed time, so the difference is the ballistics alone. */
    ui_vu_meter_t up;
    ui_vu_meter_init(&up);
    const uint8_t risen = ui_vu_meter_step(&up, 80U, 40U);

    ui_vu_meter_t down;
    ui_vu_meter_init(&down);
    (void)ui_vu_meter_step(&down, 100U, 10000U);
    const uint8_t fallen = ui_vu_meter_step(&down, 20U, 40U);

    assert(risen > 100U - fallen);
}

static void test_the_meter_settles_exactly_on_the_target(void)
{
    /* Movement proportional to the gap gets smaller as the gap does, and would
     * stall a percent short forever. A bar that never quite empties reads as a
     * stuck meter, so it has to land. */
    ui_vu_meter_t meter;
    ui_vu_meter_init(&meter);
    (void)ui_vu_meter_step(&meter, 100U, 10000U);
    for (int tick = 0; tick < 200; ++tick) {
        (void)ui_vu_meter_step(&meter, 0U, 10U);
    }
    assert(ui_vu_meter_step(&meter, 0U, 10U) == 0U);
}

static void test_neither_direction_overshoots(void)
{
    /* Sailing past the reading would invent a level the music never had. */
    ui_vu_meter_t meter;
    ui_vu_meter_init(&meter);
    for (int tick = 0; tick < 50; ++tick) {
        const uint8_t value = ui_vu_meter_step(&meter, 60U, 30U);
        assert(value <= 60U);
    }
    assert(ui_vu_meter_step(&meter, 60U, 30U) == 60U);

    for (int tick = 0; tick < 50; ++tick) {
        const uint8_t value = ui_vu_meter_step(&meter, 25U, 30U);
        assert(value >= 25U);
    }
    assert(ui_vu_meter_step(&meter, 25U, 30U) == 25U);
}

static void test_a_long_gap_shows_the_reading_rather_than_wrapping(void)
{
    /* The UI can be away for a while - another screen, a slow flush - and the
     * elapsed time then dwarfs the time constant. Unsigned arithmetic on the
     * movement would wrap and paint a full bar out of silence. */
    ui_vu_meter_t meter;
    ui_vu_meter_init(&meter);
    (void)ui_vu_meter_step(&meter, 40U, 10U);
    assert(ui_vu_meter_step(&meter, 0U, 60000U) == 0U);
    assert(ui_vu_meter_step(&meter, 70U, 60000U) == 70U);
}

static void test_a_target_above_the_scale_is_clamped(void)
{
    ui_vu_meter_t meter;
    ui_vu_meter_init(&meter);
    assert(ui_vu_meter_step(&meter, 200U, 10000U) == 100U);
}

static void test_the_meter_tolerates_being_handed_nothing(void)
{
    ui_vu_meter_init(NULL);
    assert(ui_vu_meter_step(NULL, 50U, 10U) == 0U);
}


/* A block-rate reading against a 10 ms loop: the passes in between have
 * nothing to report, and used to be drawn as silence. */
static uint8_t run_loop(uint32_t block_ms, uint16_t level, uint32_t total_ms)
{
    ui_vu_meter_t meter;
    ui_vu_meter_init(&meter);
    uint8_t value = 0U;
    uint32_t since_block = block_ms;
    for (uint32_t elapsed = 0U; elapsed < total_ms; elapsed += 10U) {
        const bool fresh = since_block >= block_ms;
        if (fresh) since_block = 0U;
        since_block += 10U;
        value = ui_vu_meter_advance(&meter, fresh, fresh ? level : 0U, 10U);
    }
    return value;
}

static void test_the_bar_reaches_the_level_whatever_the_block_rate(void)
{
    /* The failure this was written for: a FLAC block is 93 ms, so eight of
     * every nine passes had no reading. Taken as silence they pulled the bar
     * back down faster than the ninth could raise it, and music sitting at
     * three fifths of the scale was displayed in the bottom tenth. */
    const uint16_t level = 7338U;  /* -13 dBFS, 60% of the bar */
    assert(run_loop(26U, level, 2000U) == 60U);
    assert(run_loop(93U, level, 2000U) == 60U);
    assert(run_loop(200U, level, 2000U) == 60U);
}

static void test_a_gap_between_blocks_is_not_silence(void)
{
    ui_vu_meter_t meter;
    ui_vu_meter_init(&meter);
    ui_vu_meter_advance(&meter, true, 7338U, 10U);
    for (uint32_t elapsed = 10U; elapsed < UI_VU_READING_GAP_MS; elapsed += 10U) {
        assert(ui_vu_meter_advance(&meter, false, 0U, 10U) > 0U);
    }
    assert(meter.target == 60U);
}

static void test_audio_that_stops_empties_the_bar(void)
{
    /* Held only as long as a block could plausibly explain the gap. Playback
     * that ends - a track finishing, a stream stalling - has to fall to rest
     * rather than freezing at whatever was playing. */
    ui_vu_meter_t meter;
    ui_vu_meter_init(&meter);
    ui_vu_meter_advance(&meter, true, 18426U, 10U);
    uint8_t value = 100U;
    for (uint32_t elapsed = 0U; elapsed < 2000U; elapsed += 10U) {
        value = ui_vu_meter_advance(&meter, false, 0U, 10U);
    }
    assert(value == 0U);
}

static void test_a_silent_block_empties_the_bar_at_once(void)
{
    /* A reading of zero is a real measurement, not a missing one: a quiet
     * passage should fall on the release curve, not sit out the hold. */
    ui_vu_meter_t meter;
    ui_vu_meter_init(&meter);
    ui_vu_meter_advance(&meter, true, 18426U, 10U);
    const uint8_t after_one = ui_vu_meter_advance(&meter, true, 0U, 10U);
    assert(meter.target == 0U);
    assert(after_one < 100U);
}

static void test_advance_tolerates_being_handed_nothing(void)
{
    assert(ui_vu_meter_advance(NULL, true, 18426U, 10U) == 0U);
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
    test_the_scale_is_wide_enough_to_look_alive();
    test_the_scale_rises_monotonically();
    test_very_quiet_signals_read_as_empty();
    test_the_meter_rises_quickly_but_not_in_one_step();
    test_the_meter_rises_faster_than_it_falls();
    test_the_meter_settles_exactly_on_the_target();
    test_neither_direction_overshoots();
    test_a_long_gap_shows_the_reading_rather_than_wrapping();
    test_a_target_above_the_scale_is_clamped();
    test_the_meter_tolerates_being_handed_nothing();
    test_the_bar_reaches_the_level_whatever_the_block_rate();
    test_a_gap_between_blocks_is_not_silence();
    test_audio_that_stops_empties_the_bar();
    test_a_silent_block_empties_the_bar_at_once();
    test_advance_tolerates_being_handed_nothing();
    test_segment_count_is_exact_at_both_ends();
    test_segment_count_rounds_to_nearest();
    test_a_faint_signal_still_lights_one_segment();
    test_segment_count_never_exceeds_the_bar();
    test_the_red_zone_sits_at_the_top();
    puts("ui_vu_meter tests passed");
    return 0;
}
