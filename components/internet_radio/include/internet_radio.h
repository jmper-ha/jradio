#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#include "icy_metadata.h"
#include "internet_radio_state.h"
#include "station_catalog.h"

#define INTERNET_RADIO_TITLE_MAX_LEN ICY_METADATA_TITLE_MAX_LEN

typedef struct {
    internet_radio_state_t state;
    size_t station_index;
    char station[32];
    char title[INTERNET_RADIO_TITLE_MAX_LEN];
    char codec[8];
    uint16_t bitrate_kbps;
    uint32_t sample_rate_hz;
    /* How full the decoder's input buffer is, 0..100. Reads ~50 on a healthy
     * stream because the read loop targets half the buffer; see
     * radio_prebuffer_percent(). */
    uint8_t buffer_percent;
} internet_radio_status_t;

/* Supplies the next track of a source that is a chain of finite tracks rather
 * than an endless stream - a Yandex Music station. Returns a URL that stays
 * valid until the next call, or NULL when the chain cannot go on; `title`
 * receives the line to show while it plays.
 *
 * A function pointer rather than a direct call because the chain lives in the
 * yandex_music component, and the audio path must not depend on it: what this
 * file knows is "an HTTP stream that ends and is followed by another one",
 * which is all it needs to know. */
typedef const char *(*internet_radio_track_source_fn)(char *title, size_t title_size);
void internet_radio_set_track_source(internet_radio_track_source_fn source);

/* Starts such a chain under `display_name`. The first track is fetched here,
 * so this blocks for as long as the API calls take. */
bool internet_radio_start_track_chain(const char *display_name);

/* Ends the current track early and moves the chain on. False when what is
 * playing is not a chain: a station has no next track, only a next station. */
bool internet_radio_skip_track(void);

esp_err_t internet_radio_init(void);
esp_err_t internet_radio_start(void);
esp_err_t internet_radio_stop(void);
esp_err_t internet_radio_pause(void);
esp_err_t internet_radio_resume(void);
void internet_radio_get_status(internet_radio_status_t *status);
size_t internet_radio_station_count(void);
const station_catalog_entry_t *internet_radio_station_at(size_t index);
bool internet_radio_start_saved_station(void);
bool internet_radio_saved_station_index(size_t *index);
bool internet_radio_start_station_index(size_t index);
size_t internet_radio_current_station_index(void);
/* Replaces the saved station catalog. Never stops playback itself: if the
 * station that was playing is gone from the new list, *out_active_station_removed
 * is set and it is the caller's job to stop the source (stopping blocks for
 * seconds, which must not happen on the HTTP server task). */
esp_err_t internet_radio_catalog_replace(const char *text, size_t length, size_t *out_count,
                                         bool *out_active_station_removed);
