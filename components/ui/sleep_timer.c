#include "sleep_timer.h"

#include <stddef.h>

void sleep_timer_init(sleep_timer_t *timer)
{
    if (timer == NULL) return;
    timer->armed = false;
    timer->minutes = 0U;
    timer->deadline_ms = 0U;
}

void sleep_timer_arm(sleep_timer_t *timer, uint16_t minutes, uint32_t now_ms)
{
    if (timer == NULL) return;
    if (minutes == 0U) {
        sleep_timer_init(timer);
        return;
    }
    if (minutes > SLEEP_TIMER_MAX_MINUTES) minutes = (uint16_t)SLEEP_TIMER_MAX_MINUTES;
    timer->armed = true;
    timer->minutes = minutes;
    timer->deadline_ms = now_ms + (uint32_t)minutes * 60U * 1000U;
}

bool sleep_timer_armed(const sleep_timer_t *timer)
{
    return timer != NULL && timer->armed;
}

uint16_t sleep_timer_minutes(const sleep_timer_t *timer)
{
    return sleep_timer_armed(timer) ? timer->minutes : 0U;
}

uint32_t sleep_timer_remaining_seconds(const sleep_timer_t *timer, uint32_t now_ms)
{
    if (!sleep_timer_armed(timer)) return 0U;
    /* Unsigned subtraction, so the millisecond counter wrapping mid-countdown
     * is a difference that stays correct rather than a timer that suddenly
     * has forty-nine days to run. */
    const uint32_t left_ms = timer->deadline_ms - now_ms;
    /* Past the deadline the difference is enormous rather than negative; the
     * whole span of a timer is far below half the counter, so that is what
     * separates "due" from "a long way to go". */
    if (left_ms > (uint32_t)SLEEP_TIMER_MAX_MINUTES * 60U * 1000U) return 0U;
    return (left_ms + 999U) / 1000U;
}

uint16_t sleep_timer_remaining_minutes(const sleep_timer_t *timer, uint32_t now_ms)
{
    const uint32_t seconds = sleep_timer_remaining_seconds(timer, now_ms);
    if (seconds == 0U) return 0U;
    return (uint16_t)((seconds + 59U) / 60U);
}

bool sleep_timer_take_expired(sleep_timer_t *timer, uint32_t now_ms)
{
    if (!sleep_timer_armed(timer)) return false;
    if (sleep_timer_remaining_seconds(timer, now_ms) != 0U) return false;
    sleep_timer_init(timer);
    return true;
}

#ifdef ESP_PLATFORM

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static sleep_timer_t s_timer;
static SemaphoreHandle_t s_timer_lock;

static uint32_t sleep_timer_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* Taken around every read as well as every write: the web server's task arms
 * it while the UI task is reading the remainder to draw, and a half-updated
 * deadline would be a countdown that jumps. */
static void sleep_timer_lock(void)
{
    if (s_timer_lock != NULL) xSemaphoreTake(s_timer_lock, portMAX_DELAY);
}

static void sleep_timer_unlock(void)
{
    if (s_timer_lock != NULL) xSemaphoreGive(s_timer_lock);
}

void sleep_timer_service_init(void)
{
    if (s_timer_lock == NULL) s_timer_lock = xSemaphoreCreateMutex();
    sleep_timer_init(&s_timer);
}

void sleep_timer_service_set(uint16_t minutes)
{
    sleep_timer_lock();
    sleep_timer_arm(&s_timer, minutes, sleep_timer_now_ms());
    sleep_timer_unlock();
}

uint16_t sleep_timer_service_minutes(void)
{
    sleep_timer_lock();
    const uint16_t minutes = sleep_timer_minutes(&s_timer);
    sleep_timer_unlock();
    return minutes;
}

uint32_t sleep_timer_service_remaining_seconds(void)
{
    sleep_timer_lock();
    const uint32_t seconds = sleep_timer_remaining_seconds(&s_timer, sleep_timer_now_ms());
    sleep_timer_unlock();
    return seconds;
}

uint16_t sleep_timer_service_remaining_minutes(void)
{
    sleep_timer_lock();
    const uint16_t minutes = sleep_timer_remaining_minutes(&s_timer, sleep_timer_now_ms());
    sleep_timer_unlock();
    return minutes;
}

bool sleep_timer_service_take_expired(void)
{
    sleep_timer_lock();
    const bool expired = sleep_timer_take_expired(&s_timer, sleep_timer_now_ms());
    sleep_timer_unlock();
    return expired;
}

#endif /* ESP_PLATFORM */
