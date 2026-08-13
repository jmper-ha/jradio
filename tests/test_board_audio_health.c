#include <assert.h>
#include <stdio.h>

#include "board_audio_health.h"

/* 44.1 kHz stereo 16-bit is 176400 B/s, so a 10 s window of real time is
 * 1764000 bytes. The numbers below are derived from that. */
#define WINDOW_MS 10000U
#define WINDOW_BYTES 1764000U

static void test_a_pipeline_keeping_up_reads_as_realtime(void)
{
    assert(board_audio_health_realtime_percent(WINDOW_BYTES, 44100U, WINDOW_MS) == 100U);
    assert(board_audio_health_realtime_percent(WINDOW_BYTES / 2U, 44100U, WINDOW_MS) == 50U);
    assert(board_audio_health_realtime_percent(0U, 44100U, WINDOW_MS) == 0U);
}

static void test_the_percentage_follows_the_configured_sample_rate(void)
{
    /* The same byte count is only half of real time once the rate doubles;
     * reading the window against a stale rate is what makes a healthy stream
     * look starved. */
    assert(board_audio_health_realtime_percent(WINDOW_BYTES, 88200U, WINDOW_MS) == 50U);
    assert(board_audio_health_realtime_percent(WINDOW_BYTES, 22050U, WINDOW_MS) == 200U);
}

static void test_an_unusable_window_reports_zero_rather_than_dividing_by_it(void)
{
    assert(board_audio_health_realtime_percent(WINDOW_BYTES, 0U, WINDOW_MS) == 0U);
    assert(board_audio_health_realtime_percent(WINDOW_BYTES, 44100U, 0U) == 0U);
    /* A window too short to contain a single byte of audio must not divide by
     * a truncated-to-zero expectation. */
    assert(board_audio_health_realtime_percent(WINDOW_BYTES, 1U, 1U) == 0U);
}

static void test_a_long_window_does_not_overflow_the_percentage(void)
{
    /* An hour at 48 kHz is ~691 MB; x100 overflows 32 bits. */
    assert(board_audio_health_realtime_percent(691200000U, 48000U, 3600000U) == 100U);
}

static void test_accumulating_a_window_tracks_the_loudest_sample(void)
{
    board_audio_health_t health;
    board_audio_health_reset(&health);
    assert(health.bytes == 0U && health.peak == 0U && health.writes == 0U);

    board_audio_health_add(&health, 4096U, 4096U, 1200U);
    board_audio_health_add(&health, 4096U, 4096U, 9000U);
    board_audio_health_add(&health, 4096U, 4096U, 300U);

    assert(health.bytes == 12288U);
    assert(health.writes == 3U);
    assert(health.short_writes == 0U);
    /* The peak is the window maximum, not the last block: a single loud block
     * proves the pipeline is carrying audio even if silence follows. */
    assert(health.peak == 9000U);
}

static void test_a_short_write_is_counted_separately_from_a_failed_one(void)
{
    board_audio_health_t health;
    board_audio_health_reset(&health);

    board_audio_health_add(&health, 1024U, 4096U, 500U);
    assert(health.short_writes == 1U);
    assert(health.writes == 1U);
    /* Only what I2S accepted counts towards real time; the dropped remainder
     * is the audible gap we are trying to detect. */
    assert(health.bytes == 1024U);

    board_audio_health_add(&health, 4096U, 4096U, 500U);
    assert(health.short_writes == 1U);
    assert(health.bytes == 5120U);
}

static void test_silence_inside_a_loud_window_is_still_counted(void)
{
    /* The case the window peak cannot see: mostly music, a burst of digital
     * silence in the middle. The peak stays high and real time stays perfect,
     * so silent_writes is the only field that shows the fault. */
    board_audio_health_t health;
    board_audio_health_reset(&health);
    board_audio_health_add(&health, 4608U, 4608U, 22000U);
    board_audio_health_add(&health, 4608U, 4608U, 0U);
    board_audio_health_add(&health, 4608U, 4608U, 0U);
    board_audio_health_add(&health, 4608U, 4608U, 21000U);

    assert(health.peak == 22000U);
    assert(health.silent_writes == 2U);
    assert(health.writes == 4U);
    assert(board_audio_health_realtime_percent(health.bytes, 44100U, 104U) == 100U);
}

static void test_a_write_that_moved_nothing_is_not_counted_as_silence(void)
{
    /* A zero-length write carries no samples at all, so calling it silence
     * would invent a fault out of an empty call. */
    board_audio_health_t health;
    board_audio_health_reset(&health);
    board_audio_health_add(&health, 0U, 4608U, 0U);
    assert(health.silent_writes == 0U);
    assert(health.short_writes == 1U);
}

static void test_a_silent_window_is_distinguishable_from_a_stalled_one(void)
{
    /* This is the whole point of the counter pair: both cases are inaudible,
     * and only these two fields tell them apart. */
    board_audio_health_t silent;
    board_audio_health_reset(&silent);
    board_audio_health_add(&silent, WINDOW_BYTES, WINDOW_BYTES, 0U);
    assert(silent.peak == 0U);
    assert(silent.silent_writes == 1U);
    assert(board_audio_health_realtime_percent(silent.bytes, 44100U, WINDOW_MS) == 100U);

    board_audio_health_t stalled;
    board_audio_health_reset(&stalled);
    assert(stalled.peak == 0U);
    assert(board_audio_health_realtime_percent(stalled.bytes, 44100U, WINDOW_MS) == 0U);
}

static void test_the_window_keeps_the_longest_zero_run(void)
{
    board_audio_health_t health;
    board_audio_health_reset(&health);
    assert(health.max_zero_run == 0U);

    board_audio_health_note_zero_run(&health, 300U);
    board_audio_health_note_zero_run(&health, 1500U);
    /* A later, shorter run must not erase the one that tripped the DAC. */
    board_audio_health_note_zero_run(&health, 40U);
    assert(health.max_zero_run == 1500U);
    assert(health.max_zero_run >= BOARD_AUDIO_ZERO_DETECT_FRAMES);
}

static void test_the_accessors_tolerate_a_missing_window(void)
{
    board_audio_health_reset(NULL);
    board_audio_health_add(NULL, 4096U, 4096U, 100U);
    board_audio_health_note_zero_run(NULL, 2048U);
}

int main(void)
{
    test_a_pipeline_keeping_up_reads_as_realtime();
    test_the_percentage_follows_the_configured_sample_rate();
    test_an_unusable_window_reports_zero_rather_than_dividing_by_it();
    test_a_long_window_does_not_overflow_the_percentage();
    test_accumulating_a_window_tracks_the_loudest_sample();
    test_a_short_write_is_counted_separately_from_a_failed_one();
    test_silence_inside_a_loud_window_is_still_counted();
    test_a_write_that_moved_nothing_is_not_counted_as_silence();
    test_a_silent_window_is_distinguishable_from_a_stalled_one();
    test_the_window_keeps_the_longest_zero_run();
    test_the_accessors_tolerate_a_missing_window();
    puts("board_audio_health tests passed");
    return 0;
}
