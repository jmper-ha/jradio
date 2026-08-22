#pragma once

#include "audio_tags.h"
#include "player_control_types.h"
#include "station_catalog.h"
#include "file_player_state.h"

#ifdef __cplusplus
extern "C" {
#endif

player_playback_state_t player_playback_from_radio(internet_radio_state_t state);
player_playback_state_t player_playback_from_file(file_player_state_t state);
bool player_snapshot_equal(const player_snapshot_t *left,
                           const player_snapshot_t *right);
bool player_rssi_refresh_due(bool seen, uint32_t last_update_ms,
                             uint32_t now_ms);
/* True when `path` is the file the drive is already running, so choosing it
 * again means "take me back to it" rather than "start it over".
 *
 * Kept out of player_control_decide() because it needs the path, and the
 * decision cannot be made from the index the command carries: the browser can
 * be re-read between the press and the command being handled, and an index
 * that survives that no longer names the same file. */
bool player_file_same_file_playing(const char *path, const char *playing_path,
                                  player_playback_state_t state);

#ifdef ESP_PLATFORM
#include "esp_err.h"
#include "file_browser.h"

esp_err_t player_control_init(void);
bool player_control_post(const player_command_t *command);
void player_control_get_snapshot(player_snapshot_t *snapshot);
const station_catalog_entry_t *player_control_station_at(size_t index);
// Copies out a row of the USB listing; false when the row is gone, which
// happens when the drive is pulled while the browser screen is open.
bool player_control_file_entry_at(size_t index, file_browser_entry_t *entry);
// Changes every time the listing is replaced, so a screen can tell "the same
// directory changed" from "this is a different directory".
unsigned int player_control_listing_revision(void);

/* How full the active source's decoder input buffer is, 0..100. False when
 * there is nothing to report - no source playing, or a WAV file, which is read
 * straight to I2S without a backlog.
 *
 * Deliberately outside player_snapshot_t. This changes on every pass of the
 * decode loop, and the web transport sends a diff whenever the snapshot
 * differs, so carrying it there would push a frame several times a second for
 * a number only the local screen shows. */
bool player_control_input_fill(uint8_t *percent);

/* Position and length of the playing track, in seconds. False when the idea
 * does not apply - a radio stream has no end, and a file's length is unknown
 * until its first frame is decoded.
 *
 * Outside player_snapshot_t for the same reason as the buffer fill: the web
 * transport sends a diff whenever the snapshot differs, and a value that ticks
 * would push a frame a second for something only the local screen shows. */
bool player_control_track_progress(uint32_t *elapsed_seconds, uint32_t *total_seconds);

/* Title, performer and album of the playing file, as its own tags gave them.
 * False for a source that has no such thing: a stream carries one ICY line and
 * no performer field at all.
 *
 * Outside player_snapshot_t for the same reason as the position above - the
 * snapshot is what the web transport diffs and sends on every change, and
 * three more strings would not fit a frame that is capped at 512 bytes. The
 * cover is not here either; it is published by album_art. */
bool player_control_track_tags(audio_tags_t *tags);

/* Reopens the directory holding `path` and starts that file, for resuming what
 * was playing before a restart. Runs on the caller's task and reads a
 * directory off the drive, so it stalls that task for as long as the read
 * takes - fine once at startup, not per poll. Returns false when the drive, the
 * directory or the file is gone; the listing is then left on whatever did
 * open, so the browser has somewhere to show. */
bool player_control_file_resume_path(const char *path);
#endif

#ifdef __cplusplus
}
#endif
