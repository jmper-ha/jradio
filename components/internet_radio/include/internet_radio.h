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
} internet_radio_status_t;

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
