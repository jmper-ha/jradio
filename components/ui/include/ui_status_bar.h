#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Derivations for the player screen's status strip - the row that carries what
 * is not about the music. Pure so the thresholds and the formatting can be
 * checked without a device. */

#define UI_STATUS_WIFI_BARS 4

/* How many of the four bars a signal lights, 0 when there is no connection.
 *
 * The thresholds are the ones the Wi-Fi world settled on: about -55 dBm is as
 * good as it gets indoors, -67 is the floor for streaming without stutter, -78
 * is where a link starts dropping. Zero bars means "not connected" and is
 * deliberately unreachable by a weak signal - a connected radio that is barely
 * hanging on must still look different from one that is not connected at all,
 * because the fix for the two is different. */
uint8_t ui_status_wifi_bars(bool valid, int8_t rssi_dbm);

/* Whether a pending volume change has settled long enough to be written to
 * flash.
 *
 * The setting has to reach the audio path on the very next block, but it must
 * not reach the file that often: saving is a read-modify-write of settings.csv
 * under a mutex, tens of milliseconds on LittleFS, and doing it per encoder
 * click blocked the UI task so hard that clicks queued up and arrived in
 * bursts. Waiting for the knob to stop turning fixes the feel and spares the
 * flash. Unsigned subtraction so a tick wrap does not defer the write forever. */
bool ui_volume_commit_due(bool pending, uint32_t changed_ms, uint32_t now_ms,
                          uint32_t settle_ms);

/* "21:47", or "--:--" before the clock has been set. The placeholder keeps the
 * strip's shape and says plainly that the time is unknown rather than showing
 * a plausible wrong one. */
void ui_status_clock_text(char *text, size_t text_size, bool valid, int hour, int minute);
