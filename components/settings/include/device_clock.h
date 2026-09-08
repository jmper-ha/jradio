#pragma once

#include <stdbool.h>

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

/* Local hour and minute. False until the first synchronisation, and it stays
 * true afterwards even if the network goes away - the oscillator keeps
 * counting, and a clock that drifts a little beats a clock that blanks. */
bool device_clock_now(int *hour, int *minute);
