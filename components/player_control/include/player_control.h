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

/* Posts PLAYER_COMMAND_TEST_STREAM together with the address to try. The URL
 * is copied into a buffer of player_control's own rather than into the
 * command: it is 256 bytes, the queue holds eight commands, and every one of
 * them would have carried the space. An empty URL stops the test.
 *
 * Only the web server sends this, and esp_http_server runs one worker, so the
 * buffer has a single writer. */
bool player_control_post_stream_test(const char *url, const char *name);
void player_control_get_snapshot(player_snapshot_t *snapshot);
const station_catalog_entry_t *player_control_station_at(size_t index);
// Copies out a row of the USB listing; false when the row is gone, which
// happens when the drive is pulled while the browser screen is open.
bool player_control_file_entry_at(size_t index, file_browser_entry_t *entry);
// Changes every time either listing is replaced, so a screen can tell "the
// same directory changed" from "this is a different directory", and so the web
// knows to re-fetch names it is no longer sent over the socket.
unsigned int player_control_listing_revision(void);
/* Says a listing this module does not own has changed - the station catalogue,
 * saved from the web, or a refreshed Yandex catalogue. The directory listing
 * bumps the counter by itself; these two are replaced elsewhere and would
 * otherwise leave every browser showing the old names. */
void player_control_note_listing_changed(void);

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
 * Outside player_snapshot_t for the same reason as the position above: the
 * snapshot is copied onto the stack of every task that polls, and three more
 * strings would cost 384 bytes on each of them. Both faces ask for them here
 * instead and pass them to ui_now_playing.h, which is what makes the screen
 * and the web page say the same thing about one track; the snapshot only
 * carries track_tag_revision, so a watcher knows when to ask again. The cover
 * is not here either; it is published by album_art. */
bool player_control_track_tags(audio_tags_t *tags);

/* The full path of the file being played, for writing the resume point down.
 * False when nothing is playing, and when the buffer is too short - never a
 * truncated path, which names a file that does not exist.
 *
 * It has to come from here rather than be assembled from the snapshot's
 * directory and track name: browsing moves that directory while the track goes
 * on playing, and the pair then describes a file that was never opened. That
 * is exactly what used to be saved - walk into another album while something
 * plays, and the resume point pointed at the playing track's name inside the
 * album you had walked into. */
bool player_control_playing_file_path(char *out, size_t out_size);

/* Reopens the directory holding `path` and starts that file, for resuming what
 * was playing before a restart. Runs on the caller's task and reads a
 * directory off the drive, so it stalls that task for as long as the read
 * takes - fine once at startup, not per poll. Returns false when the drive, the
 * directory or the file is gone; the listing is then left on whatever did
 * open, so the browser has somewhere to show. */
bool player_control_file_resume_path(const char *path);

/* The Yandex station on the air, by identity rather than by row.
 *
 * The row is what the dashboard happened to hand out this time, and it is not
 * what a resume can be written down as; this is. False when no station has
 * been started since boot.
 *
 * `id`, `name` and `from` may each be NULL if the caller wants only some of
 * them, and a buffer too short is refused rather than filled with half a
 * value. */
bool player_control_playing_yandex_station(char *id, size_t id_size, char *name,
                                           size_t name_size, char *from, size_t from_size);

/* Hands the controller the station to come back to, before asking it to play.
 *
 * Autoplay is the only caller: at boot nothing has been started, so "play
 * again" has nothing to name until the point read off settings.csv is put
 * here. Starting is a separate ask - PLAYER_COMMAND_PLAY - because it blocks
 * on three API calls and must happen on the player task, not on whichever one
 * read the settings. */
void player_control_set_yandex_station(const char *id, const char *name, const char *from);
#endif

#ifdef __cplusplus
}
#endif
