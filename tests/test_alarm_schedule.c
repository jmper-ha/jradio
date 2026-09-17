#include "alarm_schedule.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Weekdays as struct tm counts them, which is what this file is built on. */
#define SUN 0
#define MON 1
#define TUE 2
#define WED 3
#define THU 4
#define FRI 5
#define SAT 6

#define DAY(n) ((uint8_t)(1U << (n)))
#define WEEKDAYS (DAY(MON) | DAY(TUE) | DAY(WED) | DAY(THU) | DAY(FRI))

static alarm_config_t weekday_alarm(void)
{
    return (alarm_config_t){
        .enabled = true, .hour = 7U, .minute = 30U, .days = WEEKDAYS, .station = 3U,
        .volume = 40U};
}

static void test_an_alarm_that_cannot_ring(void)
{
    alarm_config_t config = weekday_alarm();
    assert(alarm_config_valid(&config));

    config.enabled = false;
    assert(!alarm_config_valid(&config));
    config = weekday_alarm();
    /* No station chosen: the switch is on and there is nothing to play. */
    config.station = 0U;
    assert(!alarm_config_valid(&config));
    config = weekday_alarm();
    /* An empty mask cannot be set through the setters and must not ring if it
     * somehow reaches here - a hand-edited settings.csv, say. */
    config.days = 0U;
    assert(!alarm_config_valid(&config));
    config = weekday_alarm();
    config.hour = 24U;
    assert(!alarm_config_valid(&config));

    uint16_t minutes = 0U;
    config.hour = 7U;
    config.enabled = false;
    assert(!alarm_schedule_next_minutes(&config, MON, 6, 0, &minutes));
    assert(!alarm_schedule_due(&config, MON, 7, 30));
}

static void test_later_today_and_tomorrow(void)
{
    const alarm_config_t config = weekday_alarm();
    uint16_t minutes = 0U;

    assert(alarm_schedule_next_minutes(&config, MON, 6, 0, &minutes));
    assert(minutes == 90U);
    /* A minute past it, so today's is gone: Tuesday at half past seven. */
    assert(alarm_schedule_next_minutes(&config, MON, 7, 31, &minutes));
    assert(minutes == 1439U);
}

/* The one that is easy to get wrong: Friday evening has to reach over the
 * weekend to Monday, and Saturday has to reach forward two days. */
static void test_the_week_wraps(void)
{
    const alarm_config_t config = weekday_alarm();
    uint16_t minutes = 0U;

    assert(alarm_schedule_next_minutes(&config, FRI, 22, 0, &minutes));
    assert(minutes == 2U * 1440U + 9U * 60U + 30U);
    assert(alarm_schedule_next_minutes(&config, SAT, 7, 30, &minutes));
    assert(minutes == 2U * 1440U);
    assert(alarm_schedule_next_minutes(&config, SUN, 23, 59, &minutes));
    assert(minutes == 7U * 60U + 31U);
}

/* Strictly forward: asked during its own minute, the answer is the next
 * occurrence, never zero. With one day set that is a whole week - which is
 * what stops the sleep button pressed while it rings from arming a wake-up a
 * second later. */
static void test_its_own_minute_answers_with_the_next_one(void)
{
    alarm_config_t config = weekday_alarm();
    uint16_t minutes = 0U;

    assert(alarm_schedule_next_minutes(&config, WED, 7, 30, &minutes));
    assert(minutes == 1440U);

    config.days = DAY(WED);
    assert(alarm_schedule_next_minutes(&config, WED, 7, 30, &minutes));
    assert(minutes == ALARM_WEEK_MINUTES);
}

static void test_the_seconds_count_down_inside_the_minute(void)
{
    const alarm_config_t config = weekday_alarm();
    uint32_t seconds = 0U;

    assert(alarm_schedule_next_seconds(&config, MON, 7, 29, 0, &seconds));
    assert(seconds == 60U);
    assert(alarm_schedule_next_seconds(&config, MON, 7, 29, 59, &seconds));
    assert(seconds == 1U);
    assert(!alarm_schedule_next_seconds(&config, MON, 7, 29, 60, &seconds));
}

static void test_due_only_on_the_days_that_are_set(void)
{
    const alarm_config_t config = weekday_alarm();

    assert(alarm_schedule_due(&config, MON, 7, 30));
    assert(!alarm_schedule_due(&config, SAT, 7, 30));
    assert(!alarm_schedule_due(&config, MON, 7, 31));
    assert(!alarm_schedule_due(&config, MON, 8, 30));
}

/* The poll loop asks once a second; the alarm goes off once. */
static void test_one_firing_per_minute(void)
{
    const alarm_config_t config = weekday_alarm();
    alarm_guard_t guard;
    alarm_guard_init(&guard);

    assert(!alarm_guard_take_due(&guard, &config, MON, 7, 29));
    assert(alarm_guard_take_due(&guard, &config, MON, 7, 30));
    for (int again = 0; again < 60; ++again) {
        assert(!alarm_guard_take_due(&guard, &config, MON, 7, 30));
    }
    /* The clock leaves the minute, and tomorrow's rings again. */
    assert(!alarm_guard_take_due(&guard, &config, MON, 7, 31));
    assert(alarm_guard_take_due(&guard, &config, TUE, 7, 30));
}

/* A board that boots straight into the alarm's minute - which is what the last
 * hop is for - has to ring rather than wait for tomorrow. */
static void test_a_fresh_guard_fires_at_once(void)
{
    const alarm_config_t config = weekday_alarm();
    alarm_guard_t guard;
    alarm_guard_init(&guard);
    assert(alarm_guard_take_due(&guard, &config, MON, 7, 30));
}

static void test_the_two_leads(void)
{
    /* Far out: wake for the quiet check ten minutes before. */
    assert(alarm_hop_lead_seconds(8U * 3600U) == ALARM_CHECK_LEAD_SECONDS);
    /* Just past the point where a check would still leave a sleep worth
     * taking. */
    assert(alarm_hop_lead_seconds(ALARM_CHECK_LEAD_SECONDS + ALARM_MIN_SLEEP_SECONDS + 1U) ==
           ALARM_CHECK_LEAD_SECONDS);
    assert(alarm_hop_lead_seconds(ALARM_CHECK_LEAD_SECONDS + ALARM_MIN_SLEEP_SECONDS) ==
           ALARM_BOOT_LEAD_SECONDS);
    assert(alarm_hop_lead_seconds(30U) == ALARM_BOOT_LEAD_SECONDS);
}

static void test_the_timer_armed_when_going_to_sleep(void)
{
    const alarm_config_t config = weekday_alarm();

    /* Monday half past ten at night, alarm at half past seven: nine hours to
     * go, so the board wakes ten minutes before it for the quiet check. */
    const uint32_t nine_hours = 9U * 3600U;
    assert(alarm_sleep_wake_after_seconds(&config, true, MON, 22, 30, 0) ==
           nine_hours - ALARM_CHECK_LEAD_SECONDS);

    /* Five minutes before: the last hop, so the board sleeps until a minute
     * before the alarm and boots there. */
    assert(alarm_sleep_wake_after_seconds(&config, true, MON, 7, 25, 0) ==
           5U * 60U - ALARM_BOOT_LEAD_SECONDS);

    /* Half a minute before, which is already inside that lead: the board wakes
     * at once, and the boot is the delay the user will hear. */
    assert(alarm_sleep_wake_after_seconds(&config, true, MON, 7, 29, 30) == 1U);

    /* No clock and no alarm both mean "the button is the only way back". */
    assert(alarm_sleep_wake_after_seconds(&config, false, MON, 22, 30, 0) == 0U);
    alarm_config_t off = config;
    off.enabled = false;
    assert(alarm_sleep_wake_after_seconds(&off, true, MON, 22, 30, 0) == 0U);
}

static void test_what_the_quiet_wake_decides(void)
{
    const alarm_config_t config = weekday_alarm();
    uint32_t sleep_seconds = 0U;

    /* Woken ten minutes out, and the corrected clock agrees: one more hop, to
     * a minute before the alarm. */
    assert(alarm_boot_decide(&config, true, 600U, &sleep_seconds) == ALARM_BOOT_SLEEP_AGAIN);
    assert(sleep_seconds == 600U - ALARM_BOOT_LEAD_SECONDS);

    /* The clock was slow and the alarm is nearly here: stay up. */
    assert(alarm_boot_decide(&config, true, 90U, &sleep_seconds) == ALARM_BOOT_PROCEED);
    assert(alarm_boot_decide(&config, true, 91U, &sleep_seconds) == ALARM_BOOT_SLEEP_AGAIN);
    assert(sleep_seconds == 31U);

    /* Hours out - the clock was wildly off, or the alarm was moved while the
     * board slept: the long lead again. */
    assert(alarm_boot_decide(&config, true, 4U * 3600U, &sleep_seconds) ==
           ALARM_BOOT_SLEEP_AGAIN);
    assert(sleep_seconds == 4U * 3600U - ALARM_CHECK_LEAD_SECONDS);

    /* Switched off while the board slept, or no clock at all: an ordinary
     * boot, which is what leaves the device on the home screen. */
    alarm_config_t off = config;
    off.enabled = false;
    assert(alarm_boot_decide(&off, true, 600U, &sleep_seconds) == ALARM_BOOT_NO_ALARM);
    assert(alarm_boot_decide(&config, false, 600U, &sleep_seconds) == ALARM_BOOT_NO_ALARM);
}

static void test_the_time_text(void)
{
    uint8_t hour = 0U;
    uint8_t minute = 0U;

    assert(alarm_time_parse("07:30", &hour, &minute) && hour == 7U && minute == 30U);
    assert(alarm_time_parse("00:00", &hour, &minute) && hour == 0U && minute == 0U);
    assert(alarm_time_parse("23:59", &hour, &minute) && hour == 23U && minute == 59U);
    /* Shapes that are not refused would be guessed at, and a guess here is an
     * alarm at a time nobody set. */
    assert(!alarm_time_parse("7:30", &hour, &minute));
    assert(!alarm_time_parse("07:30:00", &hour, &minute));
    assert(!alarm_time_parse("24:00", &hour, &minute));
    assert(!alarm_time_parse("07:60", &hour, &minute));
    assert(!alarm_time_parse("0a:30", &hour, &minute));
    assert(!alarm_time_parse("", &hour, &minute));

    char text[6];
    alarm_time_format(text, sizeof(text), 7U, 5U);
    assert(strcmp(text, "07:05") == 0);
    alarm_time_format(text, sizeof(text), 23U, 59U);
    assert(strcmp(text, "23:59") == 0);
}

int main(void)
{
    test_an_alarm_that_cannot_ring();
    test_later_today_and_tomorrow();
    test_the_week_wraps();
    test_its_own_minute_answers_with_the_next_one();
    test_the_seconds_count_down_inside_the_minute();
    test_due_only_on_the_days_that_are_set();
    test_one_firing_per_minute();
    test_a_fresh_guard_fires_at_once();
    test_the_two_leads();
    test_the_timer_armed_when_going_to_sleep();
    test_what_the_quiet_wake_decides();
    test_the_time_text();
    puts("alarm_schedule tests passed");
    return 0;
}
