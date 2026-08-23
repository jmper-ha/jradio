#include "yandex_auth.h"

/* Bounds for whatever the server suggests. The observed answer is a 5-second
 * interval and a 300-second lifetime; these limits only exist so a malformed
 * or hostile answer cannot turn the poll loop into a spin or make the code
 * expire before anyone could read it off the screen. */
#define YANDEX_AUTH_INTERVAL_DEFAULT_S 5U
#define YANDEX_AUTH_INTERVAL_MIN_S 1U
#define YANDEX_AUTH_INTERVAL_MAX_S 30U
#define YANDEX_AUTH_LIFETIME_DEFAULT_S 300U
#define YANDEX_AUTH_LIFETIME_MIN_S 30U
#define YANDEX_AUTH_LIFETIME_MAX_S 3600U
#define YANDEX_AUTH_BACKOFF_STEP_MS 5000U

static uint32_t yandex_auth_elapsed(uint32_t now_ms, uint32_t since_ms)
{
    /* Unsigned subtraction, so an uptime that wraps past 49 days still yields
     * the true gap instead of a huge one that would expire the code at once. */
    return now_ms - since_ms;
}

yandex_auth_action_t yandex_auth_flow_next(const yandex_auth_flow_t *flow, uint32_t now_ms)
{
    if (flow == NULL || flow->state != YANDEX_AUTH_WAITING) {
        return YANDEX_AUTH_ACTION_WAIT;
    }
    if (yandex_auth_elapsed(now_ms, flow->issued_ms) >= flow->lifetime_ms) {
        return YANDEX_AUTH_ACTION_EXPIRE;
    }
    /* A poll already on the wire must finish first: two in parallel would ask
     * the same question twice and could consume the confirmation twice. */
    if (flow->poll_in_flight) {
        return YANDEX_AUTH_ACTION_WAIT;
    }
    if (yandex_auth_elapsed(now_ms, flow->last_poll_ms) >= flow->interval_ms) {
        return YANDEX_AUTH_ACTION_POLL_TOKEN;
    }
    return YANDEX_AUTH_ACTION_WAIT;
}

uint32_t yandex_auth_flow_seconds_left(const yandex_auth_flow_t *flow, uint32_t now_ms)
{
    if (flow == NULL || flow->state != YANDEX_AUTH_WAITING) {
        return 0U;
    }
    const uint32_t elapsed = yandex_auth_elapsed(now_ms, flow->issued_ms);
    if (elapsed >= flow->lifetime_ms) {
        return 0U;
    }
    /* Rounded up: showing "0 seconds left" while the code still works reads as
     * a dead code and makes the user start over for nothing. */
    return (flow->lifetime_ms - elapsed + 999U) / 1000U;
}

uint32_t yandex_auth_flow_backoff(uint32_t interval_ms)
{
    const uint32_t widened = interval_ms + YANDEX_AUTH_BACKOFF_STEP_MS;
    const uint32_t limit = YANDEX_AUTH_INTERVAL_MAX_S * 1000U;
    /* The addition cannot wrap for any interval this module produces, but the
     * caller's value is not necessarily one of ours. */
    if (widened < interval_ms || widened > limit) {
        return limit;
    }
    return widened;
}

uint32_t yandex_auth_flow_clamp_interval(uint32_t interval_s)
{
    uint32_t seconds = interval_s == 0U ? YANDEX_AUTH_INTERVAL_DEFAULT_S : interval_s;
    if (seconds < YANDEX_AUTH_INTERVAL_MIN_S) seconds = YANDEX_AUTH_INTERVAL_MIN_S;
    if (seconds > YANDEX_AUTH_INTERVAL_MAX_S) seconds = YANDEX_AUTH_INTERVAL_MAX_S;
    /* Clamped in seconds before the multiplication, so an absurd value from the
     * server cannot overflow into a small millisecond count. */
    return seconds * 1000U;
}

uint32_t yandex_auth_flow_clamp_lifetime(uint32_t lifetime_s)
{
    uint32_t seconds = lifetime_s == 0U ? YANDEX_AUTH_LIFETIME_DEFAULT_S : lifetime_s;
    if (seconds < YANDEX_AUTH_LIFETIME_MIN_S) seconds = YANDEX_AUTH_LIFETIME_MIN_S;
    if (seconds > YANDEX_AUTH_LIFETIME_MAX_S) seconds = YANDEX_AUTH_LIFETIME_MAX_S;
    return seconds * 1000U;
}
