#include "sleep_timer.h"

#include <assert.h>
#include <stdio.h>

#define MINUTE_MS (60U * 1000U)

static void test_arming_sets_the_whole_span(void)
{
    sleep_timer_t timer;
    sleep_timer_init(&timer);
    assert(!sleep_timer_armed(&timer));
    assert(sleep_timer_remaining_minutes(&timer, 1000U) == 0U);

    sleep_timer_arm(&timer, 45U, 1000U);
    assert(sleep_timer_armed(&timer));
    assert(sleep_timer_minutes(&timer) == 45U);
    assert(sleep_timer_remaining_minutes(&timer, 1000U) == 45U);
    assert(sleep_timer_remaining_seconds(&timer, 1000U) == 45U * 60U);
}

/* The number on the panel: forty seconds left is "1 minute", not "0". Zero is
 * reserved for "no timer", which is what the whole indicator keys off. */
static void test_the_last_minute_reads_one(void)
{
    sleep_timer_t timer;
    sleep_timer_init(&timer);
    sleep_timer_arm(&timer, 30U, 0U);

    assert(sleep_timer_remaining_minutes(&timer, 1U) == 30U);
    assert(sleep_timer_remaining_minutes(&timer, 29U * MINUTE_MS) == 1U);
    assert(sleep_timer_remaining_minutes(&timer, 30U * MINUTE_MS - 40U * 1000U) == 1U);
    assert(sleep_timer_remaining_minutes(&timer, 30U * MINUTE_MS - 1U) == 1U);
}

static void test_expiry_is_reported_once(void)
{
    sleep_timer_t timer;
    sleep_timer_init(&timer);
    sleep_timer_arm(&timer, 15U, 0U);

    assert(!sleep_timer_take_expired(&timer, 15U * MINUTE_MS - 1U));
    assert(sleep_timer_take_expired(&timer, 15U * MINUTE_MS));
    /* And the board does not fall asleep again a poll later. */
    assert(!sleep_timer_take_expired(&timer, 15U * MINUTE_MS + 10U));
    assert(!sleep_timer_armed(&timer));
}

/* Zero is the page's "off": one path into the module rather than a second
 * call the caller might forget. */
static void test_zero_minutes_cancels(void)
{
    sleep_timer_t timer;
    sleep_timer_init(&timer);
    sleep_timer_arm(&timer, 60U, 0U);
    sleep_timer_arm(&timer, 0U, MINUTE_MS);
    assert(!sleep_timer_armed(&timer));
    assert(!sleep_timer_take_expired(&timer, 10U * MINUTE_MS));
}

static void test_a_longer_run_than_allowed_is_clamped(void)
{
    sleep_timer_t timer;
    sleep_timer_init(&timer);
    sleep_timer_arm(&timer, 5000U, 0U);
    assert(sleep_timer_minutes(&timer) == SLEEP_TIMER_MAX_MINUTES);
}

/* The millisecond counter wraps every 49 days, and a device left playing is
 * exactly the one that sees it. Unsigned arithmetic carries the countdown
 * across the wrap; the test is here because the natural signed reading of it
 * would leave a timer that never fires. */
static void test_the_countdown_survives_the_clock_wrapping(void)
{
    sleep_timer_t timer;
    sleep_timer_init(&timer);
    const uint32_t before_wrap = 0xFFFFFFFFU - 60U * 1000U;
    sleep_timer_arm(&timer, 5U, before_wrap);

    assert(sleep_timer_remaining_minutes(&timer, before_wrap) == 5U);
    /* One minute in, and the counter has just gone round. */
    assert(sleep_timer_remaining_minutes(&timer, 30U * 1000U) == 4U);
    assert(sleep_timer_take_expired(&timer, 4U * MINUTE_MS + 1U));
}

int main(void)
{
    test_arming_sets_the_whole_span();
    test_the_last_minute_reads_one();
    test_expiry_is_reported_once();
    test_zero_minutes_cancels();
    test_a_longer_run_than_allowed_is_clamped();
    test_the_countdown_survives_the_clock_wrapping();
    puts("sleep_timer tests passed");
    return 0;
}
