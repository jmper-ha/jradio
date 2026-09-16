#pragma once

#include <stdbool.h>
#include <stdint.h>

/* The sleep timer: play for so many minutes, then stop and go to sleep.
 *
 * Two halves, in one file. The model below is pure and host-tested - the
 * arithmetic is where the mistakes are, and every one of them is a device
 * that sleeps at the wrong moment. Under it is the one instance the device
 * runs, which the web server arms from its task and the UI task watches; a
 * lock separates the two, and nothing else here knows about the timer.
 *
 * It is deliberately not a setting and is not written to the card: what is
 * saved would be a deadline, and a deadline restored after a reboot means a
 * device that switches itself off some minutes after coming back, which is
 * the least explicable thing it could do. */

/* The ceiling for whatever arrives. Twelve hours is far past falling asleep
 * to the radio and still only a fiftieth of what the 32-bit millisecond
 * counter holds, which is what lets "past the deadline" below be told from "a
 * long way to go" by size alone. */
#define SLEEP_TIMER_MAX_MINUTES 720U

typedef struct {
    bool armed;
    /* What was asked for, kept so the page can show the choice that is set
     * rather than derive it from a shrinking remainder. */
    uint16_t minutes;
    uint32_t deadline_ms;
} sleep_timer_t;

void sleep_timer_init(sleep_timer_t *timer);
/* 0 minutes cancels, which is how the page turns it off - one path, not two.
 * Anything past the ceiling is clamped rather than refused. */
void sleep_timer_arm(sleep_timer_t *timer, uint16_t minutes, uint32_t now_ms);
bool sleep_timer_armed(const sleep_timer_t *timer);
uint16_t sleep_timer_minutes(const sleep_timer_t *timer);
/* Seconds left, 0 when it is not armed or already due. */
uint32_t sleep_timer_remaining_seconds(const sleep_timer_t *timer, uint32_t now_ms);
/* Minutes left, rounded up: a timer with forty seconds to go reads "1", not
 * "0". Zero means only "not running", so the panel can key the whole
 * indicator off this one number. */
uint16_t sleep_timer_remaining_minutes(const sleep_timer_t *timer, uint32_t now_ms);
/* True once, the first time the deadline has passed, and disarms the timer
 * with it: whoever asks is the one who acts on it. */
bool sleep_timer_take_expired(sleep_timer_t *timer, uint32_t now_ms);

#ifdef ESP_PLATFORM
/* The device's one timer. Same rules, no `now` to pass and safe to call from
 * any task. */
void sleep_timer_service_init(void);
void sleep_timer_service_set(uint16_t minutes);
uint16_t sleep_timer_service_minutes(void);
uint32_t sleep_timer_service_remaining_seconds(void);
uint16_t sleep_timer_service_remaining_minutes(void);
bool sleep_timer_service_take_expired(void);
#endif
