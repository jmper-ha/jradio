#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "audio_tags.h"
#include "esp_err.h"
#include "file_browser.h"
#include "file_player_state.h"

#define FILE_PLAYER_TRACK_MAX_LEN 128
#define FILE_PLAYER_CODEC_MAX_LEN 8

typedef struct {
    file_player_state_t state;
    // File name only, not the full path: this is what the player screen shows
    // where the radio shows a station name.
    char track[FILE_PLAYER_TRACK_MAX_LEN];
    char codec[FILE_PLAYER_CODEC_MAX_LEN];
    uint32_t sample_rate_hz;
    uint16_t bitrate_kbps;
    /* How full the decoder's input buffer is, 0..100. Reads near 100 on a
     * healthy track: unlike the network path, this one refills to the brim on
     * every pass because a file always has the next bytes ready. */
    uint8_t buffer_percent;
    /* Position and length of the track, in seconds. `total_seconds` is 0 until
     * the first frame reveals the bitrate, and stays an estimate on a
     * variable-bitrate file - see file_track_progress.h. */
    uint32_t elapsed_seconds;
    uint32_t total_seconds;
    /* Bumped each time a track's tags are published. The tags themselves are
     * too big to travel in this structure (see below), but whoever only needs
     * to know that they have changed can watch this: they are read after the
     * track has already started, so nothing else in here moves when they
     * arrive, and a watcher comparing the rest would never notice. */
    uint32_t tags_revision;
} file_player_status_t;

// Called from the playback task once a track ends *by itself* - not after a
// stop and not after a failure. It runs on the playback task as it exits, so
// it must not call back into file_player_play(): post the intent somewhere and
// return.
typedef void (*file_player_finished_cb_t)(void);
void file_player_set_finished_callback(file_player_finished_cb_t callback);

esp_err_t file_player_init(void);

// Starts `path`, replacing whatever was playing. Blocks until the previous
// track's task has released the I2S output, so the caller must not be the task
// that owns a short poll deadline.
esp_err_t file_player_play(const char *path, const char *display_name,
                          file_browser_format_t format);
esp_err_t file_player_stop(void);
esp_err_t file_player_pause(void);
esp_err_t file_player_resume(void);

/* Moves the playing track to `seconds` from its start. The jump is applied by
 * the playback task on its next pass, so this returns before the audio has
 * moved - and does nothing at all if the track ends first.
 *
 * Where the file lands is exact for WAV and an estimate from the average
 * bitrate for everything else; see file_track_seek_offset(). A paused track
 * stays paused: the request is held and applied on the pass that resumes it,
 * since the playback task sleeps in its pause loop until then. */
esp_err_t file_player_seek(uint32_t seconds);
void file_player_get_status(file_player_status_t *status);

/* What the playing file's own tags say, all empty when it has none.
 *
 * Deliberately not part of file_player_status_t: that structure is copied onto
 * the stack of every task that polls the player, and three more strings would
 * cost 384 bytes on each of them - for something one screen reads. The file
 * name in `track` stays where it is for the same reason it always was: it is
 * what identifies the track on the drive and what gets written down as the
 * resume point, which a tag title could never lead back to. */
void file_player_get_tags(audio_tags_t *tags);
