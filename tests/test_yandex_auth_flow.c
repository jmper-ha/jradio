#include <assert.h>
#include <stdio.h>

#include "yandex_auth.h"

static yandex_auth_flow_t waiting_flow(void)
{
    return (yandex_auth_flow_t){
        .state = YANDEX_AUTH_WAITING,
        .issued_ms = 1000U,
        .last_poll_ms = 1000U,
        .interval_ms = 5000U,
        .lifetime_ms = 300000U,
        .poll_in_flight = false,
    };
}

static void test_nothing_happens_until_a_code_is_outstanding(void)
{
    yandex_auth_flow_t flow = waiting_flow();
    for (int state = YANDEX_AUTH_IDLE; state <= YANDEX_AUTH_FAILED; ++state) {
        if (state == YANDEX_AUTH_WAITING) continue;
        flow.state = (yandex_auth_state_t)state;
        assert(yandex_auth_flow_next(&flow, 999999U) == YANDEX_AUTH_ACTION_WAIT);
        assert(yandex_auth_flow_seconds_left(&flow, 999999U) == 0U);
    }
}

static void test_the_first_poll_waits_out_one_interval(void)
{
    /* The server hands out the code and an interval in the same answer; asking
     * immediately would poll before the user could possibly have typed it. */
    const yandex_auth_flow_t flow = waiting_flow();
    assert(yandex_auth_flow_next(&flow, 1000U) == YANDEX_AUTH_ACTION_WAIT);
    assert(yandex_auth_flow_next(&flow, 5999U) == YANDEX_AUTH_ACTION_WAIT);
    assert(yandex_auth_flow_next(&flow, 6000U) == YANDEX_AUTH_ACTION_POLL_TOKEN);
}

static void test_a_poll_on_the_wire_blocks_the_next_one(void)
{
    /* Two polls in parallel would ask the same question twice, and the second
     * answer could arrive after the flow has already moved on. */
    yandex_auth_flow_t flow = waiting_flow();
    flow.poll_in_flight = true;
    assert(yandex_auth_flow_next(&flow, 60000U) == YANDEX_AUTH_ACTION_WAIT);
    flow.poll_in_flight = false;
    assert(yandex_auth_flow_next(&flow, 60000U) == YANDEX_AUTH_ACTION_POLL_TOKEN);
}

static void test_expiry_wins_over_a_due_poll(void)
{
    /* Polling a dead code only earns an error; the screen must be told to stop
     * showing it instead. */
    const yandex_auth_flow_t flow = waiting_flow();
    assert(yandex_auth_flow_next(&flow, 1000U + 299999U) == YANDEX_AUTH_ACTION_POLL_TOKEN);
    assert(yandex_auth_flow_next(&flow, 1000U + 300000U) == YANDEX_AUTH_ACTION_EXPIRE);
    assert(yandex_auth_flow_next(&flow, 1000U + 400000U) == YANDEX_AUTH_ACTION_EXPIRE);
}

static void test_the_clock_wrapping_does_not_kill_a_live_code(void)
{
    /* Uptime is a 32-bit millisecond counter and wraps after 49 days. A signed
     * or naive comparison would make every code look ancient right then. */
    yandex_auth_flow_t flow = waiting_flow();
    flow.issued_ms = 0xFFFFF000U;
    flow.last_poll_ms = 0xFFFFF000U;
    const uint32_t after_wrap = 0xFFFFF000U + 6000U; /* wraps past zero */
    assert(yandex_auth_flow_next(&flow, after_wrap) == YANDEX_AUTH_ACTION_POLL_TOKEN);
    assert(yandex_auth_flow_seconds_left(&flow, after_wrap) == 294U);
}

static void test_the_countdown_never_shows_zero_while_the_code_still_works(void)
{
    const yandex_auth_flow_t flow = waiting_flow();
    assert(yandex_auth_flow_seconds_left(&flow, 1000U) == 300U);
    assert(yandex_auth_flow_seconds_left(&flow, 1500U) == 300U); /* rounded up */
    assert(yandex_auth_flow_seconds_left(&flow, 1000U + 299999U) == 1U);
    assert(yandex_auth_flow_seconds_left(&flow, 1000U + 300000U) == 0U);
}

static void test_a_hostile_interval_cannot_spin_or_stall_the_poll(void)
{
    assert(yandex_auth_flow_clamp_interval(5U) == 5000U);
    assert(yandex_auth_flow_clamp_interval(0U) == 5000U);   /* missing field */
    assert(yandex_auth_flow_clamp_interval(9999U) == 30000U);
    /* Clamped in seconds before the multiplication: a raw value near UINT32_MAX
     * would otherwise overflow into a tiny millisecond count and spin. */
    assert(yandex_auth_flow_clamp_interval(4294967U) == 30000U);
    assert(yandex_auth_flow_clamp_interval(4294967295U) == 30000U);
}

static void test_a_hostile_lifetime_stays_readable(void)
{
    assert(yandex_auth_flow_clamp_lifetime(300U) == 300000U);
    assert(yandex_auth_flow_clamp_lifetime(0U) == 300000U);
    assert(yandex_auth_flow_clamp_lifetime(1U) == 30000U);
    assert(yandex_auth_flow_clamp_lifetime(4294967295U) == 3600000U);
}

static void test_slow_down_widens_the_gap_but_stays_bounded(void)
{
    assert(yandex_auth_flow_backoff(5000U) == 10000U);
    assert(yandex_auth_flow_backoff(28000U) == 30000U);
    assert(yandex_auth_flow_backoff(30000U) == 30000U);
    assert(yandex_auth_flow_backoff(4294967295U) == 30000U);
}

static void test_bad_arguments_are_survivable(void)
{
    assert(yandex_auth_flow_next(NULL, 0U) == YANDEX_AUTH_ACTION_WAIT);
    assert(yandex_auth_flow_seconds_left(NULL, 0U) == 0U);
}

int main(void)
{
    test_nothing_happens_until_a_code_is_outstanding();
    test_the_first_poll_waits_out_one_interval();
    test_a_poll_on_the_wire_blocks_the_next_one();
    test_expiry_wins_over_a_due_poll();
    test_the_clock_wrapping_does_not_kill_a_live_code();
    test_the_countdown_never_shows_zero_while_the_code_still_works();
    test_a_hostile_interval_cannot_spin_or_stall_the_poll();
    test_a_hostile_lifetime_stays_readable();
    test_slow_down_widens_the_gap_but_stays_bounded();
    test_bad_arguments_are_survivable();
    printf("yandex_auth_flow tests passed\n");
    return 0;
}
