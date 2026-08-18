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

void device_clock_init(void);

/* Local hour and minute. False until the first synchronisation, and it stays
 * true afterwards even if the network goes away - the oscillator keeps
 * counting, and a clock that drifts a little beats a clock that blanks. */
bool device_clock_now(int *hour, int *minute);
