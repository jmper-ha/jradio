#include "alarm_schedule.h"

#include <stdio.h>

/* Minutes from Sunday 00:00, which is what every distance here is computed on:
 * one number for the whole week turns "next Tuesday at seven" into a
 * subtraction and a modulo, and there is no other way to get the wrap right
 * without a table of cases. */
#define MINUTES_PER_DAY 1440U

static unsigned int week_minute(int weekday, int hour, int minute)
{
    return (unsigned int)weekday * MINUTES_PER_DAY + (unsigned int)hour * 60U +
           (unsigned int)minute;
}

static bool moment_valid(int weekday, int hour, int minute)
{
    return weekday >= 0 && weekday <= 6 && hour >= 0 && hour <= 23 && minute >= 0 &&
           minute <= 59;
}

bool alarm_config_valid(const alarm_config_t *config)
{
    if (config == NULL) return false;
    if (!config->enabled) return false;
    if (config->station == 0U) return false;
    if (config->days == 0U || config->days > ALARM_DAYS_ALL) return false;
    if (config->hour > 23U || config->minute > 59U) return false;
    return config->volume <= 100U;
}

bool alarm_schedule_due(const alarm_config_t *config, int weekday, int hour, int minute)
{
    if (!alarm_config_valid(config) || !moment_valid(weekday, hour, minute)) return false;
    if ((config->days & (uint8_t)(1U << weekday)) == 0U) return false;
    return hour == (int)config->hour && minute == (int)config->minute;
}

bool alarm_schedule_next_minutes(const alarm_config_t *config, int weekday, int hour,
                                 int minute, uint16_t *minutes_ahead)
{
    if (!alarm_config_valid(config) || !moment_valid(weekday, hour, minute)) return false;
    if (minutes_ahead == NULL) return false;

    const unsigned int now = week_minute(weekday, hour, minute);
    unsigned int best = ALARM_WEEK_MINUTES;
    for (int day = 0; day < 7; ++day) {
        if ((config->days & (uint8_t)(1U << day)) == 0U) continue;
        const unsigned int at = week_minute(day, (int)config->hour, (int)config->minute);
        /* Strictly forward, so the alarm's own minute answers with the next
         * occurrence rather than with itself: the +ALARM_WEEK_MINUTES before
         * the modulo is what turns a distance of zero into a whole week. */
        const unsigned int ahead =
            (at + ALARM_WEEK_MINUTES - now - 1U) % ALARM_WEEK_MINUTES + 1U;
        if (ahead < best) best = ahead;
    }
    *minutes_ahead = (uint16_t)best;
    return true;
}

bool alarm_schedule_next_seconds(const alarm_config_t *config, int weekday, int hour,
                                 int minute, int second, uint32_t *seconds_ahead)
{
    if (second < 0 || second > 59 || seconds_ahead == NULL) return false;
    uint16_t minutes = 0U;
    if (!alarm_schedule_next_minutes(config, weekday, hour, minute, &minutes)) return false;
    *seconds_ahead = (uint32_t)minutes * 60U - (uint32_t)second;
    return true;
}

void alarm_guard_init(alarm_guard_t *guard)
{
    if (guard == NULL) return;
    guard->armed = true;
}

bool alarm_guard_take_due(alarm_guard_t *guard, const alarm_config_t *config, int weekday,
                          int hour, int minute)
{
    if (guard == NULL) return false;
    if (!alarm_schedule_due(config, weekday, hour, minute)) {
        guard->armed = true;
        return false;
    }
    if (!guard->armed) return false;
    guard->armed = false;
    return true;
}

uint32_t alarm_hop_lead_seconds(uint32_t seconds_to_alarm)
{
    /* The quiet check only earns its Wi-Fi if there is a real sleep left after
     * it; closer than that and this hop is the last one. */
    if (seconds_to_alarm > ALARM_CHECK_LEAD_SECONDS + ALARM_MIN_SLEEP_SECONDS) {
        return ALARM_CHECK_LEAD_SECONDS;
    }
    return ALARM_BOOT_LEAD_SECONDS;
}

uint32_t alarm_sleep_wake_after_seconds(const alarm_config_t *config, bool clock_valid,
                                        int weekday, int hour, int minute, int second)
{
    if (!clock_valid) return 0U;
    uint32_t ahead = 0U;
    if (!alarm_schedule_next_seconds(config, weekday, hour, minute, second, &ahead)) {
        return 0U;
    }
    const uint32_t lead = alarm_hop_lead_seconds(ahead);
    /* An alarm already inside the lead wakes the board immediately rather than
     * being missed: one second is the smallest timer worth arming, and the
     * boot that follows is the delay the user will hear. */
    return ahead > lead ? ahead - lead : 1U;
}

alarm_boot_action_t alarm_boot_decide(const alarm_config_t *config, bool clock_valid,
                                      uint32_t seconds_to_alarm, uint32_t *sleep_seconds)
{
    if (!alarm_config_valid(config) || !clock_valid) return ALARM_BOOT_NO_ALARM;
    if (seconds_to_alarm <= ALARM_BOOT_LEAD_SECONDS + ALARM_MIN_SLEEP_SECONDS) {
        return ALARM_BOOT_PROCEED;
    }
    const uint32_t lead = alarm_hop_lead_seconds(seconds_to_alarm);
    if (sleep_seconds != NULL) *sleep_seconds = seconds_to_alarm - lead;
    return ALARM_BOOT_SLEEP_AGAIN;
}

#ifdef ESP_PLATFORM
static bool s_boot_pending;

void alarm_boot_mark_pending(void) { s_boot_pending = true; }

bool alarm_boot_pending(void) { return s_boot_pending; }

bool alarm_boot_take_pending(void)
{
    const bool pending = s_boot_pending;
    s_boot_pending = false;
    return pending;
}
#endif

static bool two_digits(const char *text, uint8_t *out)
{
    if (text[0] < '0' || text[0] > '9' || text[1] < '0' || text[1] > '9') return false;
    *out = (uint8_t)((text[0] - '0') * 10 + (text[1] - '0'));
    return true;
}

bool alarm_time_parse(const char *text, uint8_t *hour, uint8_t *minute)
{
    if (text == NULL || hour == NULL || minute == NULL) return false;
    if (text[0] == '\0' || text[1] == '\0' || text[2] != ':' || text[3] == '\0' ||
        text[4] == '\0' || text[5] != '\0') {
        return false;
    }
    uint8_t parsed_hour = 0U;
    uint8_t parsed_minute = 0U;
    if (!two_digits(text, &parsed_hour) || !two_digits(text + 3, &parsed_minute)) return false;
    if (parsed_hour > 23U || parsed_minute > 59U) return false;
    *hour = parsed_hour;
    *minute = parsed_minute;
    return true;
}

void alarm_time_format(char *out, size_t size, uint8_t hour, uint8_t minute)
{
    if (out == NULL || size == 0U) return;
    snprintf(out, size, "%02u:%02u", (unsigned int)(hour % 24U), (unsigned int)(minute % 60U));
}
