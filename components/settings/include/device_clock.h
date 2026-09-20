#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Wall-clock time, kept by SNTP.
 *
 * The device has no RTC and no battery, so after a power cut it knows nothing
 * about the time until it has both a network and an answer from a time server.
 * Every reader therefore has to handle "not set yet" - which is why this
 * returns a validity flag rather than a plausible-looking 00:00.
 *
 * Starting it is safe before Wi-Fi is up: SNTP retries on its own, and the
 * clock simply stays unset until the first reply arrives. */

/* `server` is a host name for SNTP and `timezone_id` an id out of
 * device_timezone.h; either may be NULL or unknown, which leaves the built-in
 * defaults standing. */
void device_clock_init(const char *server, const char *timezone_id);

/* The same two, changed while the device runs - the web interface is the only
 * place they can be set. The zone takes effect on the next reading of the
 * clock; a new server costs SNTP a restart, so it is only restarted when the
 * name actually changed. */
void device_clock_apply(const char *server, const char *timezone_id);
/* The zone alone, for a boot that reads the clock the RTC kept through a deep
 * sleep and must not bring up SNTP - it has no network and is about to go
 * back down. */
void device_clock_set_timezone(const char *timezone_id);

/* Local hour and minute. False until the first synchronisation, and it stays
 * true afterwards even if the network goes away - the oscillator keeps
 * counting, and a clock that drifts a little beats a clock that blanks. */
bool device_clock_now(int *hour, int *minute);
/* Everything the alarm needs in one reading, so the four fields cannot come
 * from two sides of a minute boundary: weekday counting from Sunday as 0, the
 * hour, the minute and the second. False until the first synchronisation -
 * except after a deep sleep, where the RTC kept counting and the clock is
 * usable the moment the chip comes back, drift and all. */
bool device_clock_moment(int *weekday, int *hour, int *minute, int *second);

/* Blocks until SNTP reports a fresh answer or the timeout runs out; true when
 * one arrived. Only the alarm's quiet wake uses it: the clock it woke with is
 * the one the RTC's internal oscillator kept through the night, and this is
 * what trims the minutes of drift off it before the last hop is measured. */
bool device_clock_wait_sync(uint32_t timeout_ms);

/* The local date, under the same rule: false until the first synchronisation.
 * `month` is 1..12 and `weekday` counts from Sunday as 0, the way struct tm
 * does, because that is what every reader has to feed a name table with. */
bool device_clock_today(int *day, int *month, int *weekday);
