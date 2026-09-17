#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The alarm clock: what is set, when it next goes off, and how long the board
 * may sleep before it does.
 *
 * All of it is arithmetic on the wall clock, which is exactly the part that is
 * easy to get wrong and impossible to see going wrong - a device that rings on
 * the wrong day rings once a week. So it lives here, pure and host-tested, and
 * the device files only ask it questions. Nothing in here reads the clock: the
 * caller passes what `device_clock_now()` and `device_clock_today()` gave it.
 *
 * Days are a bitmask over `struct tm.tm_wday` - bit 0 is Sunday - because that
 * is the number the C library hands out and converting it twice is a bug
 * waiting to happen. The page shows Monday first; that is the page's business.
 */

/* Every day. Also the default: a mask is never empty - the setter refuses zero
 * and the page will not let the last day be unticked - because an alarm with
 * no days is a switch that is on and does nothing. */
#define ALARM_DAYS_ALL 0x7FU

/* Minutes in a week, which is what a distance to the next firing is measured
 * in and the largest one there is: the same minute next week. */
#define ALARM_WEEK_MINUTES 10080U

typedef struct {
    bool enabled;
    uint8_t hour;   /* 0..23 */
    uint8_t minute; /* 0..59 */
    /* Bit N is weekday N, Sunday first. Never 0 - see ALARM_DAYS_ALL. */
    uint8_t days;
    /* The station's number as the panel and the page print it, counting from
     * 1, not the catalog index. That is what the user chose from the list, and
     * a number is what survives an edit that inserts a row above it about as
     * well as an index does - but at least it is the number they saw. 0 means
     * no station has been chosen and the alarm cannot ring. */
    uint8_t station;
    uint8_t volume; /* 0..100 */
} alarm_config_t;

/* Whether this configuration could ever ring: switched on, a station chosen,
 * and the numbers in range. */
bool alarm_config_valid(const alarm_config_t *config);

/* Is this very minute the one? The caller asks once a second and the guard
 * below is what turns that into one firing. */
bool alarm_schedule_due(const alarm_config_t *config, int weekday, int hour, int minute);

/* Minutes from the given moment to the next firing, counted *strictly
 * forward*: a call made during the alarm's own minute answers with the one
 * after it, never 0. That is not a detail - it is what stops the sleep button
 * pressed while the alarm is ringing from arming a wake-up one second later
 * and bringing the whole device back. The range is 1..ALARM_WEEK_MINUTES.
 *
 * False when the alarm could not ring at all, which leaves `*minutes_ahead`
 * alone. */
bool alarm_schedule_next_minutes(const alarm_config_t *config, int weekday, int hour,
                                 int minute, uint16_t *minutes_ahead);

/* The same distance in seconds, given where the current minute already is.
 * At least 1, for the reason above. */
bool alarm_schedule_next_seconds(const alarm_config_t *config, int weekday, int hour,
                                 int minute, int second, uint32_t *seconds_ahead);

/* One firing per minute, however often this is asked.
 *
 * `armed` starts true so that a board which boots *into* the alarm's minute -
 * which is precisely what the last hop below is for - rings instead of waiting
 * for the next one. It is armed again as soon as the clock leaves that minute,
 * which needs no timestamp to compare against and so cannot be fooled by the
 * clock being set backwards. */
typedef struct {
    bool armed;
} alarm_guard_t;

void alarm_guard_init(alarm_guard_t *guard);
bool alarm_guard_take_due(alarm_guard_t *guard, const alarm_config_t *config, int weekday,
                          int hour, int minute);

/* How early the board wakes for the quiet check: far enough out that the
 * drift it is there to correct - the RTC counts on an internal RC oscillator
 * while the chip sleeps, and a night of it is worth minutes - cannot have
 * pushed the alarm past this point. Nothing is switched on for it: no panel,
 * no peripheral rail, only Wi-Fi long enough to hear a time server. */
#define ALARM_CHECK_LEAD_SECONDS 600U
/* How early the board boots for real: enough for Wi-Fi, the catalog and a
 * buffer, so the sound starts at the minute rather than a boot after it. */
#define ALARM_BOOT_LEAD_SECONDS 60U
/* Shorter than this is not worth a sleep - the board would spend it booting
 * again - so the last hop just stays awake. */
#define ALARM_MIN_SLEEP_SECONDS 30U

/* The lead for the hop being taken now: the long one while there is still
 * room for a quiet check before it, the short one otherwise. */
uint32_t alarm_hop_lead_seconds(uint32_t seconds_to_alarm);

/* The timer to arm when going to sleep, in seconds; 0 when there is no alarm
 * to wake for and the button is the only way back. Never 0 when there is one -
 * an alarm closer than the lead wakes the board at once, which is the honest
 * answer to "sleep now, ring in twenty seconds". */
uint32_t alarm_sleep_wake_after_seconds(const alarm_config_t *config, bool clock_valid,
                                        int weekday, int hour, int minute, int second);

typedef enum {
    /* Nothing to wake for: carry on with the ordinary boot. */
    ALARM_BOOT_NO_ALARM = 0,
    /* Too early still - back to sleep for `sleep_seconds`, screen dark. */
    ALARM_BOOT_SLEEP_AGAIN,
    /* Close enough: finish booting and let it ring. */
    ALARM_BOOT_PROCEED,
} alarm_boot_action_t;

/* What a board woken by the timer should do, once it has had a chance to
 * correct its clock. `seconds_to_alarm` is what alarm_schedule_next_seconds()
 * answered afterwards. */
alarm_boot_action_t alarm_boot_decide(const alarm_config_t *config, bool clock_valid,
                                      uint32_t seconds_to_alarm, uint32_t *sleep_seconds);

#ifdef ESP_PLATFORM
/* This boot exists because the alarm is about to go off.
 *
 * Set by the boot path before anything else runs and taken once by the UI, so
 * that autoplay does not resume last night's station a minute before the
 * alarm switches to this morning's - and so the panel stays dark until it
 * rings. A plain flag needs no lock: it is written while app_main is still the
 * only task and read after that. */
void alarm_boot_mark_pending(void);
bool alarm_boot_pending(void);
bool alarm_boot_take_pending(void);
#endif

/* "HH:MM" both ways, since the setting is stored and sent as text. The parser
 * takes exactly five characters and two-digit fields - "7:5" is refused, not
 * guessed at. */
bool alarm_time_parse(const char *text, uint8_t *hour, uint8_t *minute);
void alarm_time_format(char *out, size_t size, uint8_t hour, uint8_t minute);
