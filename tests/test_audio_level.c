#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_level.h"
#include "audio_tone.h"
#include "audio_volume.h"

#define RATE 48000U
#define NEAR(value, want, tolerance) (fabsf((value) - (want)) <= (tolerance))
// One block of the shape the radio hands the board: 1152 frames, an MP3 frame.
#define CHUNK 1152U

static int16_t s_buffer[CHUNK * 2U];
static double s_phase;

/* A sine of `dbfs` peak in both channels; 0 dBFS is full scale. */
static void fill_sine(float dbfs, float freq, uint32_t rate)
{
    const double amplitude = 32767.0 * pow(10.0, dbfs / 20.0);
    for (size_t i = 0U; i < CHUNK; ++i) {
        const int16_t v = (int16_t)lrint(amplitude * sin(s_phase));
        s_buffer[i * 2U] = v;
        s_buffer[i * 2U + 1U] = v;
        s_phase += 2.0 * 3.14159265358979 * freq / rate;
    }
}

static void play(audio_level_t *level, float dbfs, float seconds, uint32_t rate)
{
    const size_t chunks = (size_t)(seconds * (float)rate / (float)CHUNK);
    for (size_t i = 0U; i < chunks; ++i) {
        if (dbfs <= -200.0f) {
            memset(s_buffer, 0, sizeof(s_buffer));
        } else {
            fill_sine(dbfs, 997.0f, rate);
        }
        audio_level_measure(level, rate, s_buffer, CHUNK);
    }
}

static void begin_fresh(audio_level_t *level)
{
    audio_level_init(level);
    audio_level_set_enabled(level, true);
    audio_level_begin(level, 0.0f, false, false);
}

/* BS.1770's own check: a 997 Hz sine at 0 dBFS in one channel reads
 * -3.01 LKFS, so in both it reads 0, and the K-weighting passes 1 kHz
 * nearly flat while it cuts the bass and lifts the treble. */
static void test_the_meter_reads_what_the_standard_says(void)
{
    audio_biquad_t k[2];
    audio_level_k_filter(RATE, k);
    const float at_1k = audio_tone_response_db(&k[0], 997.0f, RATE) +
                        audio_tone_response_db(&k[1], 997.0f, RATE);
    assert(NEAR(at_1k, 0.69f, 0.1f));
    assert(audio_tone_response_db(&k[1], 20.0f, RATE) < -6.0f);
    assert(NEAR(audio_tone_response_db(&k[0], 10000.0f, RATE), 4.0f, 0.3f));

    const uint32_t rates[] = {22050U, 44100U, 48000U, 96000U};
    for (size_t r = 0U; r < sizeof(rates) / sizeof(rates[0]); ++r) {
        audio_level_t level;
        begin_fresh(&level);
        play(&level, -20.0f, 10.0f, rates[r]);
        audio_level_status_t status;
        audio_level_status(&level, &status);
        assert(NEAR(status.loudness, -20.0f, 0.2f));
    }
}

static void test_a_new_station_is_measured_then_corrected(void)
{
    audio_level_t level;
    begin_fresh(&level);
    audio_level_status_t status;
    audio_level_status(&level, &status);
    assert(status.phase == AUDIO_LEVEL_MEASURING);
    // A loud station: -8 LUFS, so 8 dB too loud for a target of -16.
    play(&level, -8.0f, 2.0f, RATE);
    audio_level_status(&level, &status);
    assert(status.phase == AUDIO_LEVEL_MEASURING);
    assert(status.gain_db == 0.0f);
    play(&level, -8.0f, 4.0f, RATE);
    audio_level_status(&level, &status);
    assert(status.phase == AUDIO_LEVEL_LEARNING);
    assert(NEAR(status.wanted_db, -8.0f, 0.3f));
    // Caught up at about a decibel a second while the station is new.
    assert(status.gain_db < 0.0f && status.gain_db > -4.0f);
    play(&level, -8.0f, 10.0f, RATE);
    audio_level_status(&level, &status);
    assert(NEAR(status.gain_db, -8.0f, 0.3f));
    play(&level, -8.0f, 20.0f, RATE);
    audio_level_status(&level, &status);
    assert(status.phase == AUDIO_LEVEL_SETTLED);
}

static void test_pauses_and_silence_do_not_move_the_estimate(void)
{
    audio_level_t level;
    begin_fresh(&level);
    play(&level, -14.0f, 30.0f, RATE);
    audio_level_status_t before;
    audio_level_status(&level, &before);
    play(&level, -300.0f, 20.0f, RATE);  // silence: the absolute gate
    play(&level, -40.0f, 20.0f, RATE);   // a quiet intro: the relative gate
    audio_level_status_t after;
    audio_level_status(&level, &after);
    assert(NEAR(after.loudness, before.loudness, 0.05f));
    assert(NEAR(after.gain_db, before.gain_db, 0.05f));
}

static void test_one_loud_passage_moves_it_little(void)
{
    audio_level_t level;
    begin_fresh(&level);
    play(&level, -18.0f, 60.0f, RATE);
    audio_level_status_t before;
    audio_level_status(&level, &before);
    play(&level, -8.0f, 5.0f, RATE);  // an advert, 10 dB louder
    audio_level_status_t after;
    audio_level_status(&level, &after);
    assert(after.gain_db - before.gain_db > -1.0f);
}

static void test_the_slow_phase_keeps_its_speed(void)
{
    audio_level_t level;
    audio_level_init(&level);
    audio_level_set_enabled(&level, true);
    // Remembered at -2, and the station has since got 6 dB louder.
    audio_level_begin(&level, -2.0f, true, false);
    audio_level_status_t status;
    audio_level_status(&level, &status);
    assert(status.phase == AUDIO_LEVEL_SETTLED);
    assert(status.gain_db == -2.0f);
    play(&level, -10.0f, 10.0f, RATE);
    audio_level_status(&level, &status);
    assert(NEAR(status.wanted_db, -6.0f, 0.3f));
    // No faster than 0.1 dB/s once remembered: 10 s from the first
    // estimate at 3.2 s is under a decibel.
    assert(status.gain_db <= -2.0f && status.gain_db > -3.0f);
    assert(status.phase == AUDIO_LEVEL_LEARNING);
}

static void test_the_limit_and_the_dead_band(void)
{
    audio_level_t level;
    begin_fresh(&level);
    play(&level, 0.0f, 40.0f, RATE);  // 16 dB too loud
    audio_level_status_t status;
    audio_level_status(&level, &status);
    assert(status.wanted_db == -AUDIO_LEVEL_LIMIT_DB);
    // A remembered station 0.3 dB off where it should be stays put.
    audio_level_begin(&level, -1.7f, true, false);
    play(&level, -14.0f, 60.0f, RATE);
    audio_level_status(&level, &status);
    assert(status.gain_db == -1.7f);
}

static void test_a_lift_stays_inside_the_volume(void)
{
    audio_level_t level;
    audio_level_init(&level);
    audio_level_set_enabled(&level, true);
    audio_level_begin(&level, 6.0f, true, false);
    // Full volume: no room, nothing lifted, and the status says so.
    assert(audio_level_apply(&level, AUDIO_VOLUME_UNITY) == AUDIO_VOLUME_UNITY);
    audio_level_status_t status;
    audio_level_status(&level, &status);
    assert(status.capped);
    // 6 dB down: the whole lift fits, and nothing over full scale.
    const uint16_t half = AUDIO_VOLUME_UNITY / 2U;
    const uint16_t lifted = audio_level_apply(&level, half);
    assert(lifted <= AUDIO_VOLUME_UNITY && lifted > AUDIO_VOLUME_UNITY - 400U);
    audio_level_status(&level, &status);
    assert(!status.capped);
    // A cut is always there.
    audio_level_begin(&level, -6.0f, true, false);
    assert(NEAR((float)audio_level_apply(&level, AUDIO_VOLUME_UNITY),
                (float)AUDIO_VOLUME_UNITY / 2.0f, 200.0f));
    for (unsigned percent = 0U; percent <= 100U; ++percent) {
        audio_level_begin(&level, AUDIO_LEVEL_LIMIT_DB, true, false);
        assert(audio_level_apply(&level, audio_volume_gain((uint8_t)percent)) <=
               AUDIO_VOLUME_UNITY);
    }
    // Silence stays silence.
    assert(audio_level_apply(&level, 0U) == 0U);
}

static void test_tags_are_taken_as_given(void)
{
    audio_level_t level;
    audio_level_init(&level);
    audio_level_set_enabled(&level, true);
    audio_level_begin(&level, -5.5f, false, true);
    play(&level, 0.0f, 30.0f, RATE);
    audio_level_status_t status;
    audio_level_status(&level, &status);
    assert(status.fixed && status.phase == AUDIO_LEVEL_SETTLED);
    assert(status.gain_db == -5.5f);
}

static void test_switching_off_slopes_back_and_goes_idle(void)
{
    audio_level_t level;
    audio_level_init(&level);
    assert(audio_level_idle(&level));
    audio_level_begin(&level, -6.0f, true, false);  // off: ignored
    assert(audio_level_apply(&level, AUDIO_VOLUME_UNITY) == AUDIO_VOLUME_UNITY);
    audio_level_set_enabled(&level, true);
    audio_level_begin(&level, -6.0f, true, false);
    assert(!audio_level_idle(&level));
    audio_level_set_enabled(&level, false);
    play(&level, -14.0f, 0.3f, RATE);
    assert(level.gain_db < -1.0f);  // a slope, not a step
    play(&level, -14.0f, 1.0f, RATE);
    assert(audio_level_idle(&level));
    audio_level_status_t status;
    audio_level_status(&level, &status);
    assert(status.phase == AUDIO_LEVEL_OFF);
}

static void test_only_twenty_seconds_of_sound_is_kept(void)
{
    audio_level_t level;
    begin_fresh(&level);
    audio_level_status_t status;
    play(&level, -14.0f, 10.0f, RATE);
    audio_level_status(&level, &status);
    assert(status.phase != AUDIO_LEVEL_MEASURING && !status.keep);
    // Silence does not count towards it.
    play(&level, -300.0f, 30.0f, RATE);
    audio_level_status(&level, &status);
    assert(!status.keep);
    play(&level, -14.0f, 11.0f, RATE);
    audio_level_status(&level, &status);
    assert(status.keep);
    // A source's own figure is not ours to keep.
    audio_level_begin(&level, -5.0f, false, true);
    play(&level, -14.0f, 30.0f, RATE);
    audio_level_status(&level, &status);
    assert(!status.keep);
}

static void test_a_nudge_works_with_the_levelling_off_and_on(void)
{
    audio_level_t level;
    audio_level_init(&level);
    // Off: the nudge alone, and no longer idle while it is there.
    audio_level_set_nudge(&level, -6);
    assert(!audio_level_idle(&level));
    assert(NEAR((float)audio_level_apply(&level, AUDIO_VOLUME_UNITY),
                (float)AUDIO_VOLUME_UNITY / 2.0f, 200.0f));
    audio_level_status_t status;
    audio_level_status(&level, &status);
    assert(status.phase == AUDIO_LEVEL_OFF && status.nudge_db == -6);
    // Held to its limit.
    audio_level_set_nudge(&level, 40);
    assert(level.nudge_db == AUDIO_LEVEL_NUDGE_LIMIT_DB);
    // A lift inside the volume's room only, as the correction's is.
    assert(audio_level_apply(&level, AUDIO_VOLUME_UNITY) == AUDIO_VOLUME_UNITY);
    audio_level_status(&level, &status);
    assert(status.capped);
    // On: added to the correction, and kept through a begin.
    audio_level_set_enabled(&level, true);
    audio_level_set_nudge(&level, 3);
    audio_level_begin(&level, -9.0f, true, false);
    assert(level.nudge_db == 3);
    assert(NEAR((float)audio_level_apply(&level, AUDIO_VOLUME_UNITY),
                (float)AUDIO_VOLUME_UNITY / 2.0f, 200.0f));
    audio_level_set_nudge(&level, 0);
    audio_level_set_enabled(&level, false);
    play(&level, -14.0f, 2.0f, RATE);
    assert(audio_level_idle(&level));
}

static void test_a_known_loudness_is_corrected_without_measuring(void)
{
    // A Yandex track's r128 of -10.55 LUFS: 5.45 dB too loud for -16.
    assert(NEAR(audio_level_correction_db(-10.55f), -5.45f, 0.001f));
    assert(audio_level_correction_db(-40.0f) == AUDIO_LEVEL_LIMIT_DB);
    assert(audio_level_correction_db(0.0f) == -AUDIO_LEVEL_LIMIT_DB);
    assert(audio_level_correction_db(NAN) == 0.0f);
}

int main(void)
{
    test_a_known_loudness_is_corrected_without_measuring();
    test_the_meter_reads_what_the_standard_says();
    test_a_new_station_is_measured_then_corrected();
    test_pauses_and_silence_do_not_move_the_estimate();
    test_one_loud_passage_moves_it_little();
    test_the_slow_phase_keeps_its_speed();
    test_the_limit_and_the_dead_band();
    test_a_lift_stays_inside_the_volume();
    test_tags_are_taken_as_given();
    test_switching_off_slopes_back_and_goes_idle();
    test_only_twenty_seconds_of_sound_is_kept();
    test_a_nudge_works_with_the_levelling_off_and_on();
    puts("audio level tests passed");
    return 0;
}
