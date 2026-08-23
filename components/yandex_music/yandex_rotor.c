#include "yandex_rotor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "album_art.h"
#include "internet_radio.h"
#include "yandex_api.h"
#include "yandex_track.h"

static const char *TAG = "yandex_rotor";

/* Measured 2026-08-23: a batch of five tracks is 12.4 KB of JSON, and asking
 * again with ?queue= returned 12.0 KB. The margin is for a longer batch, not
 * for comfort - a body that fills the buffer is a body cut in half, and half a
 * JSON document parses as nothing. */
#define YANDEX_ROTOR_RESPONSE_SIZE 24576U

typedef struct {
    char station[YANDEX_STATION_ID_MAX + 1U];
    /* The track the next request has to name to move the chain on. Empty on
     * the first request of a station, which is what asks for its opening
     * batch. */
    char queue_from[YANDEX_TRACK_ID_MAX + 1U];
    yandex_track_batch_t batch;
    uint8_t next_index;
    /* One allocation carved into three: the JSON body, the downloadInfoUrl the
     * chosen variant points at, and the signed link handed to the audio path.
     * Carved rather than three static arrays because the two URLs are over a
     * kilobyte each, and internal SRAM is the scarcest memory on the board. */
    char *scratch;
    char *response;
    char *info_url;
    char *url;
    esp_err_t last_error;
} yandex_rotor_t;

static yandex_rotor_t s_rotor;

/* Large and short-lived: PSRAM first, internal only as a fallback, the rule
 * every big buffer in this firmware follows. */
static char *yandex_rotor_alloc(size_t size)
{
    char *buffer = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        buffer = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return buffer;
}

esp_err_t yandex_rotor_start(const char *station_id)
{
    if (station_id == NULL || station_id[0] == '\0') return ESP_ERR_INVALID_ARG;
    if (strlen(station_id) > YANDEX_STATION_ID_MAX) return ESP_ERR_INVALID_ARG;

    if (strcmp(s_rotor.station, station_id) != 0) {
        s_rotor.batch.count = 0U;
        s_rotor.next_index = 0U;
        s_rotor.queue_from[0] = '\0';
        strcpy(s_rotor.station, station_id);
    }
    if (s_rotor.scratch == NULL) {
        const size_t url_room = YANDEX_LINK_URL_MAX + 1U;
        s_rotor.scratch = yandex_rotor_alloc(YANDEX_ROTOR_RESPONSE_SIZE + 2U * url_room);
        if (s_rotor.scratch == NULL) return ESP_ERR_NO_MEM;
        s_rotor.response = s_rotor.scratch;
        s_rotor.info_url = s_rotor.scratch + YANDEX_ROTOR_RESPONSE_SIZE;
        s_rotor.url = s_rotor.info_url + url_room;
    }
    s_rotor.last_error = ESP_OK;
    return ESP_OK;
}

void yandex_rotor_stop(void)
{
    free(s_rotor.scratch);
    s_rotor.scratch = NULL;
    s_rotor.response = NULL;
    s_rotor.info_url = NULL;
    s_rotor.url = NULL;
    /* The station and its position are kept: stopping and starting the same
     * station again should not replay the tracks already heard. */
}

static esp_err_t yandex_rotor_refill(void)
{
    /* Station id, track id and the fixed parts, with room to spare: a
     * truncated path would fetch a different station's tracks. */
    char path[192];
    int length;
    if (s_rotor.queue_from[0] != '\0') {
        length = snprintf(path, sizeof(path), "/rotor/station/%s/tracks?settings2=True&queue=%s",
                          s_rotor.station, s_rotor.queue_from);
    } else {
        length = snprintf(path, sizeof(path), "/rotor/station/%s/tracks?settings2=True",
                          s_rotor.station);
    }
    if (length < 0 || (size_t)length >= sizeof(path)) return ESP_ERR_INVALID_ARG;

    int status = 0;
    const esp_err_t err =
        yandex_api_get(path, s_rotor.response, YANDEX_ROTOR_RESPONSE_SIZE, &status);
    if (err != ESP_OK) return err;
    if (status != 200) {
        ESP_LOGW(TAG, "rotor tracks returned HTTP %d", status);
        return ESP_FAIL;
    }
    if (!yandex_tracks_parse_batch(s_rotor.response, &s_rotor.batch)) {
        ESP_LOGW(TAG, "rotor tracks answer held nothing playable");
        s_rotor.batch.count = 0U;
        return ESP_ERR_NOT_FOUND;
    }
    s_rotor.next_index = 0U;
    return ESP_OK;
}

static esp_err_t yandex_rotor_resolve(const char *track_id, char *url, size_t url_size)
{
    char path[64];
    const int length = snprintf(path, sizeof(path), "/tracks/%s/download-info", track_id);
    if (length < 0 || (size_t)length >= sizeof(path)) return ESP_ERR_INVALID_ARG;

    int status = 0;
    esp_err_t err = yandex_api_get(path, s_rotor.response, YANDEX_ROTOR_RESPONSE_SIZE, &status);
    if (err != ESP_OK) return err;
    if (status != 200) {
        ESP_LOGW(TAG, "download-info for %s returned HTTP %d", track_id, status);
        return ESP_FAIL;
    }

    yandex_link_result_t picked = yandex_link_pick_variant(s_rotor.response, s_rotor.info_url,
                                                           YANDEX_LINK_URL_MAX + 1U, NULL);
    if (picked == YANDEX_LINK_ERR_PREVIEW_ONLY) return ESP_ERR_NOT_SUPPORTED;
    if (picked != YANDEX_LINK_OK) return ESP_ERR_INVALID_RESPONSE;

    /* The second hop answers with a short XML document rather than JSON, and
     * carries no token - the URL was signed by the answer above. */
    err = yandex_api_get_url(s_rotor.info_url, s_rotor.response, YANDEX_ROTOR_RESPONSE_SIZE,
                             &status);
    if (err != ESP_OK) return err;
    if (status != 200) {
        ESP_LOGW(TAG, "download-info XML returned HTTP %d", status);
        return ESP_FAIL;
    }
    if (yandex_link_from_xml(s_rotor.response, url, url_size) != YANDEX_LINK_OK) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

esp_err_t yandex_rotor_next(char *url, size_t url_size, yandex_track_t *track)
{
    if (url == NULL || url_size == 0U || track == NULL) return ESP_ERR_INVALID_ARG;
    if (s_rotor.scratch == NULL) return ESP_ERR_INVALID_STATE;
    url[0] = '\0';

    if (s_rotor.next_index >= s_rotor.batch.count) {
        const esp_err_t err = yandex_rotor_refill();
        s_rotor.last_error = err;
        if (err != ESP_OK) return err;
    }

    const yandex_track_t candidate = s_rotor.batch.tracks[s_rotor.next_index++];
    /* Recorded before the link is resolved: a track that fails to resolve has
     * still been handed out, and asking for it again would loop on it. */
    strcpy(s_rotor.queue_from, candidate.id);

    const esp_err_t err = yandex_rotor_resolve(candidate.id, url, url_size);
    s_rotor.last_error = err;
    if (err != ESP_OK) return err;

    *track = candidate;
    ESP_LOGI(TAG, "next track %s (%u ms)", candidate.id, (unsigned int)candidate.duration_ms);
    return ESP_OK;
}

/* Publishes the track's cover, or takes the previous one down.
 *
 * Failures are silent on purpose: a missing picture is a cosmetic loss, and
 * the track it belongs to is already playing by the time this runs. The
 * response buffer is reused - the XML it held has been turned into the link
 * above, so nothing else is looking at it. */
static void yandex_rotor_publish_cover(const char *cover_uri)
{
    char url[YANDEX_COVER_URL_MAX + 1U];
    if (!yandex_cover_url(cover_uri, url, sizeof(url))) {
        album_art_clear();
        return;
    }
    size_t length = 0U;
    if (yandex_api_get_image(url, (uint8_t *)s_rotor.response, YANDEX_ROTOR_RESPONSE_SIZE,
                             &length) != ESP_OK ||
        length == 0U) {
        album_art_clear();
        return;
    }
    /* Identical bytes are free: album_art keeps a checksum, so an album whose
     * tracks share one picture does not blank the tile on every change. */
    if (!album_art_set_image((const uint8_t *)s_rotor.response, length)) {
        ESP_LOGW(TAG, "cover did not decode (%u bytes)", (unsigned int)length);
    }
}

esp_err_t yandex_rotor_last_error(void)
{
    return s_rotor.last_error;
}

const char *yandex_rotor_next_url(char *title, size_t title_size)
{
    yandex_track_t track;
    if (yandex_rotor_next(s_rotor.url, YANDEX_LINK_URL_MAX + 1U, &track) != ESP_OK) {
        return NULL;
    }
    /* After the link, never before it: the picture is worth about 150 ms and
     * the sound is what the caller is waiting for. */
    yandex_rotor_publish_cover(track.cover);
    if (title != NULL && title_size > 0U) {
        /* "Performer - Title", the shape an ICY stream announces and the shape
         * every screen in this firmware already lays out. The version is part
         * of the name for a remix, so it goes in when there is room and is
         * dropped rather than allowed to push the title out. */
        int written = snprintf(title, title_size, "%s - %s", track.artist, track.title);
        if (track.version[0] != '\0' && written > 0 && (size_t)written < title_size) {
            char extended[INTERNET_RADIO_TITLE_MAX_LEN];
            const int full = snprintf(extended, sizeof(extended), "%s - %s (%s)", track.artist,
                                      track.title, track.version);
            if (full > 0 && (size_t)full < title_size) {
                snprintf(title, title_size, "%s", extended);
            }
        }
    }
    return s_rotor.url;
}
