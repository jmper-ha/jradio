#include <assert.h>
#include <stdio.h>

#include "radio_prebuffer.h"

#define CAPACITY 65536U
#define CHUNK 2048U

static radio_prebuffer_config_t config(void)
{
    radio_prebuffer_config_t value;
    radio_prebuffer_config_init(&value, CAPACITY, CHUNK);
    return value;
}

static void test_an_empty_buffer_is_allowed_to_wait_for_data(void)
{
    const radio_prebuffer_config_t cfg = config();
    const radio_prebuffer_plan_t plan = radio_prebuffer_plan(&cfg, 0U, true);

    assert(plan.should_read);
    assert(plan.budget_ms > 0U);
    assert(plan.max_bytes == CAPACITY);
}

static void test_a_backlog_below_target_reads_without_blocking(void)
{
    /* This is the case the old loop never handled: it only read when the
     * decoder was already starved, so the backlog could not grow. Reading here
     * with a zero budget is what builds the cushion on a fast link. */
    const radio_prebuffer_config_t cfg = config();
    const radio_prebuffer_plan_t plan = radio_prebuffer_plan(&cfg, CAPACITY / 4U, false);

    assert(plan.should_read);
    assert(plan.budget_ms == 0U);
    assert(plan.max_bytes == CAPACITY - CAPACITY / 4U);
}

static void test_a_comfortable_backlog_skips_the_read_entirely(void)
{
    const radio_prebuffer_config_t cfg = config();
    const radio_prebuffer_plan_t plan = radio_prebuffer_plan(&cfg, CAPACITY / 2U, false);

    assert(!plan.should_read);
}

static void test_a_nearly_empty_buffer_is_urgent_even_without_need_input(void)
{
    /* The decoder can still have a frame's worth left while being one call
     * away from starving, so waiting must not require it to hit empty first. */
    const radio_prebuffer_config_t cfg = config();
    const radio_prebuffer_plan_t plan = radio_prebuffer_plan(&cfg, CHUNK / 2U, false);

    assert(plan.should_read);
    assert(plan.budget_ms > 0U);
}

static void test_a_starved_decoder_waits_even_with_a_large_backlog(void)
{
    /* need_input means the decoder cannot proceed - a partial frame spanning
     * the buffer. Whatever the byte count says, this pass has to fetch. */
    const radio_prebuffer_config_t cfg = config();
    const radio_prebuffer_plan_t plan = radio_prebuffer_plan(&cfg, CAPACITY - CHUNK, true);

    assert(plan.should_read);
    assert(plan.budget_ms > 0U);
    assert(plan.max_bytes == CHUNK);
}

static void test_a_full_buffer_never_reads(void)
{
    /* Reading with no room would overflow the buffer. */
    const radio_prebuffer_config_t cfg = config();
    assert(!radio_prebuffer_plan(&cfg, CAPACITY, false).should_read);
    assert(!radio_prebuffer_plan(&cfg, CAPACITY, true).should_read);
    assert(!radio_prebuffer_plan(&cfg, CAPACITY + 1U, true).should_read);
}

static void test_the_blocking_budget_stays_under_the_dma_depth(void)
{
    /* The I2S DMA holds ~93 ms. A pass that blocks longer than that drains the
     * very buffer it is refilling, which is the failure an earlier uncapped
     * top-up produced. */
    const radio_prebuffer_config_t cfg = config();
    const radio_prebuffer_plan_t plan = radio_prebuffer_plan(&cfg, 0U, true);
    assert(plan.budget_ms < 93U);
}

static void test_a_missing_or_degenerate_config_reads_nothing(void)
{
    assert(!radio_prebuffer_plan(NULL, 0U, true).should_read);

    radio_prebuffer_config_t empty;
    radio_prebuffer_config_init(&empty, 0U, CHUNK);
    assert(!radio_prebuffer_plan(&empty, 0U, true).should_read);

    radio_prebuffer_config_init(NULL, CAPACITY, CHUNK);
}

static void test_backlog_converts_to_playing_time(void)
{
    /* 128 kbps is 16000 B/s, so 16000 bytes is one second. */
    assert(radio_prebuffer_millis(16000U, 128U) == 1000U);
    /* A FLAC stream at 1441 kbps holds far less time in the same bytes, which
     * is the whole reason the buffer was grown to 64 KB. */
    assert(radio_prebuffer_millis(16000U, 1441U) < 100U);
    /* Unknown bitrate before the first frame must not divide by zero. */
    assert(radio_prebuffer_millis(16000U, 0U) == 0U);
    assert(radio_prebuffer_millis(0U, 128U) == 0U);
}

static void test_a_full_buffer_of_high_bitrate_audio_still_beats_the_dma(void)
{
    /* Sanity on the sizing: 64 KB must be worth more than the 93 ms of DMA
     * even for the worst stream in the playlist, or the buffer is pointless. */
    assert(radio_prebuffer_millis(CAPACITY / 2U, 1441U) > 93U);
}


static void test_fill_percent_reads_the_whole_buffer_not_the_target(void)
{
    /* The read loop stops at half the buffer, so a perfectly healthy stream
     * reads 50 here. Scaling to the target instead would show 100 for a stream
     * holding exactly as much as one holding twice that. */
    assert(radio_prebuffer_percent(32768U, 65536U) == 50U);
    assert(radio_prebuffer_percent(65536U, 65536U) == 100U);
    assert(radio_prebuffer_percent(0U, 65536U) == 0U);
}

static void test_fill_percent_keeps_both_ends_honest(void)
{
    /* A buffer holding something must not read as empty - that is exactly the
     * state worth telling apart from silence. */
    assert(radio_prebuffer_percent(1U, 65536U) == 1U);
    assert(radio_prebuffer_percent(300U, 65536U) == 1U);
    /* The other end needs no such guard: a buffer one byte short of full is
     * full for any purpose this number serves, and rounding it down to 99
     * would be the surprising answer.
     * More than capacity cannot happen, but must not wrap if it does. */
    assert(radio_prebuffer_percent(100000U, 65536U) == 100U);
    assert(radio_prebuffer_percent(1000U, 0U) == 0U);
}

static void test_fill_percent_rounds_to_nearest(void)
{
    /* Truncating would make the meter sit a notch low all the time. */
    assert(radio_prebuffer_percent(32768U, 65536U) == 50U);
    /* 5.65% - truncating would show 5. */
    assert(radio_prebuffer_percent(3700U, 65536U) == 6U);
    assert(radio_prebuffer_percent(16384U, 32768U) == 50U);
}

static void test_the_target_is_a_cushion_and_stops_growing_with_the_buffer(void)
{
    /* At the size the target was written for, half the buffer stands. */
    radio_prebuffer_config_t small;
    radio_prebuffer_config_init(&small, 65536U, CHUNK);
    assert(small.target == 32768U);

    /* Once the buffer is bigger than that, the target holds still. Chasing
     * half of a 256 KB buffer is eight seconds of an ordinary station, and the
     * reading it takes lands on the seconds just after the output opens, when
     * the DAC has nothing behind it. */
    radio_prebuffer_config_t large;
    radio_prebuffer_config_init(&large, 262144U, CHUNK);
    assert(large.target == RADIO_PREBUFFER_TARGET_MAX);
    assert(large.capacity == 262144U);

    /* The cap is on the target only: a buffer that big is still allowed to
     * hold what a fast station delivers. */
    const radio_prebuffer_plan_t plan = radio_prebuffer_plan(&large, 0U, true);
    assert(plan.max_bytes == 262144U);
}

int main(void)
{
    test_an_empty_buffer_is_allowed_to_wait_for_data();
    test_a_backlog_below_target_reads_without_blocking();
    test_a_comfortable_backlog_skips_the_read_entirely();
    test_a_nearly_empty_buffer_is_urgent_even_without_need_input();
    test_a_starved_decoder_waits_even_with_a_large_backlog();
    test_a_full_buffer_never_reads();
    test_the_blocking_budget_stays_under_the_dma_depth();
    test_a_missing_or_degenerate_config_reads_nothing();
    test_backlog_converts_to_playing_time();
    test_a_full_buffer_of_high_bitrate_audio_still_beats_the_dma();
    test_fill_percent_reads_the_whole_buffer_not_the_target();
    test_fill_percent_keeps_both_ends_honest();
    test_fill_percent_rounds_to_nearest();
    test_the_target_is_a_cushion_and_stops_growing_with_the_buffer();
    puts("radio_prebuffer tests passed");
    return 0;
}
