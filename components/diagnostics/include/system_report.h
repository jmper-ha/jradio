#pragma once

#include <stdint.h>

/* Periodic report of the resources that fail quietly: task stack headroom and
 * internal SRAM. The watermark logic and its thresholds live in
 * system_health.h; this is the half that reads the figures off the device.
 *
 * The tasks are found by name rather than by handle, so no other component has
 * to register anything or keep a handle alive for this. Names that do not
 * exist at the moment - the decoder when nothing is playing - are simply
 * skipped and picked up again when they come back. */

/* One line at startup: why the device last restarted, and what the heap looked
 * like before anything had run. The reset reason is the thing worth having -
 * after a reboot loop it is the difference between "someone pulled the power"
 * and "it panicked", which otherwise takes a serial capture to answer. */
void system_report_boot(void);

/* Call often; it reports on its own schedule and returns immediately in
 * between. `now_ms` is any monotonic millisecond clock. */
void system_report_tick(uint32_t now_ms);
