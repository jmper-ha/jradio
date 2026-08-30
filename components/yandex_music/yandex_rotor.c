#include "yandex_rotor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "album_art.h"
#include "internet_radio.h"
#include "yandex_api.h"
#include "yandex_feedback.h"
#include "yandex_track.h"

static const char *TAG = "yandex_rotor";

/* Measured 2026-08-23: a batch of five tracks is 12.4 KB of JSON, and asking
 * again with ?queue= returned 12.0 KB. The margin is for a longer batch, not
 * for comfort - a body that fills the buffer is a body cut in half, and half a
 * JSON document parses as nothing. */
#define YANDEX_ROTOR_RESPONSE_SIZE 24576U

typedef struct {
    char station[YANDEX_STATION_ID_MAX + 1U];
    /* The station's idForFrom, kept only to name the place the listening
     * started when the chain is reported as begun. */
    char from[YANDEX_STATION_FROM_MAX + 1U];
    /* The track the next request has to name to move the chain on. Empty on
     * the first request of a station, which is what asks for its opening
     * batch. */
    char queue_from[YANDEX_TRACK_ID_MAX + 1U];
    yandex_track_batch_t batch;
    uint8_t next_index;
    /* What is on the air, so that leaving it can be reported. Cleared as soon
     * as the report is filed, which is what keeps a retried track change from
     * reporting the same play twice. */
    char playing_id[YANDEX_TRACK_ID_MAX + 1U];
    /* The like mark as the rotor reported it, kept up to date when the
     * listener changes it. */
    bool playing_liked;
    /* The dislike, which the rotor does not report - measured 2026-08-26, a
     * sequence item carries `liked` and nothing about the other list. It is
     * therefore false on every track that starts and true only where the
     * listener has just pressed for it, which loses nothing: a disliked track
     * is one the rotor stops handing out. */
    bool playing_disliked;
    char playing_batch[YANDEX_TRACK_BATCH_ID_MAX + 1U];
    /* The coverUri of the track on the air, kept so the web page can be given
     * the address and fetch a bigger copy than the panel's 96 px tile. */
    char playing_cover[YANDEX_TRACK_COVER_MAX + 1U];
    uint32_t playing_duration_ms;
    int64_t playing_since_us;
    bool playing;
    /* Set by player_control when the listener pressed for the next track. */
    bool skipped;
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

/* Guards the three fields describing the track on the air. The chain is
 * advanced by the decode task, and the screen and the web server each read
 * them on their own; a spinlock is enough because everything held under it is
 * two flags and a copy of a short array. */
static portMUX_TYPE s_playing_lock = portMUX_INITIALIZER_UNLOCKED;

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

/* Fills in the parts of an event that come from the station rather than from
 * the track, so that every report names where it came from. */
static void yandex_rotor_event_init(yandex_feedback_event_t *event,
                                    yandex_feedback_kind_t kind)
{
    *event = (yandex_feedback_event_t){0};
    event->kind = kind;
    snprintf(event->station, sizeof(event->station), "%s", s_rotor.station);
}

/* Reports leaving whatever is on the air, as a skip when the listener asked
 * for the next track and as a finish otherwise - including a stop part-way
 * through, which is still a track that was listened to for that long.
 *
 * Does nothing when nothing is playing, which is what makes it safe to call
 * from both the track change and the stop. */
static void yandex_rotor_report_end(void)
{
    if (!s_rotor.playing) return;
    taskENTER_CRITICAL(&s_playing_lock);
    s_rotor.playing = false;
    taskEXIT_CRITICAL(&s_playing_lock);

    yandex_feedback_event_t event;
    yandex_rotor_event_init(&event, s_rotor.skipped ? YANDEX_FEEDBACK_SKIP
                                                    : YANDEX_FEEDBACK_TRACK_FINISHED);
    s_rotor.skipped = false;
    snprintf(event.track_id, sizeof(event.track_id), "%s", s_rotor.playing_id);
    snprintf(event.batch_id, sizeof(event.batch_id), "%s", s_rotor.playing_batch);

    const int64_t elapsed_us = esp_timer_get_time() - s_rotor.playing_since_us;
    uint32_t played_ms = elapsed_us > 0 ? (uint32_t)(elapsed_us / 1000) : 0U;
    /* Clamped to the track: the clock starts when the link is asked for, which
     * is a second or two before the first sample is heard, and a pause stops
     * the music without stopping the clock. Reporting more seconds than the
     * track has is the one answer that is certainly wrong. */
    if (s_rotor.playing_duration_ms != 0U && played_ms > s_rotor.playing_duration_ms) {
        played_ms = s_rotor.playing_duration_ms;
    }
    event.played_ms = played_ms;
    (void)yandex_feedback_post(&event);
}

esp_err_t yandex_rotor_start(const char *station_id, const char *from)
{
    if (station_id == NULL || station_id[0] == '\0') return ESP_ERR_INVALID_ARG;
    if (strlen(station_id) > YANDEX_STATION_ID_MAX) return ESP_ERR_INVALID_ARG;

    /* Before the station is replaced, or the track just left would be reported
     * against the station taking over from it - and starting anything at all
     * means whatever was on the air is over, same station or not. */
    yandex_rotor_report_end();

    if (strcmp(s_rotor.station, station_id) != 0) {
        s_rotor.batch.count = 0U;
        s_rotor.next_index = 0U;
        s_rotor.queue_from[0] = '\0';
        strcpy(s_rotor.station, station_id);
    }
    snprintf(s_rotor.from, sizeof(s_rotor.from), "%s", from != NULL ? from : "");
    if (s_rotor.scratch == NULL) {
        const size_t url_room = YANDEX_LINK_URL_MAX + 1U;
        s_rotor.scratch = yandex_rotor_alloc(YANDEX_ROTOR_RESPONSE_SIZE + 2U * url_room);
        if (s_rotor.scratch == NULL) return ESP_ERR_NO_MEM;
        s_rotor.response = s_rotor.scratch;
        s_rotor.info_url = s_rotor.scratch + YANDEX_ROTOR_RESPONSE_SIZE;
        s_rotor.url = s_rotor.info_url + url_room;
    }
    s_rotor.last_error = ESP_OK;

    /* Sent on every start rather than only on a new station: this is the event
     * that says the listener chose to put this station on, and choosing it
     * again tomorrow is exactly the signal it exists to carry. */
    yandex_feedback_event_t event;
    yandex_rotor_event_init(&event, YANDEX_FEEDBACK_RADIO_STARTED);
    snprintf(event.from, sizeof(event.from), "%s", s_rotor.from);
    (void)yandex_feedback_post(&event);
    return ESP_OK;
}

bool yandex_rotor_playing_track(char *id, size_t id_size, bool *liked, bool *disliked)
{
    if (id == NULL || id_size == 0U) return false;
    char copy[YANDEX_TRACK_ID_MAX + 1U];
    bool playing;
    bool mark;
    bool against;
    taskENTER_CRITICAL(&s_playing_lock);
    playing = s_rotor.playing;
    mark = s_rotor.playing_liked;
    against = s_rotor.playing_disliked;
    memcpy(copy, s_rotor.playing_id, sizeof(copy));
    taskEXIT_CRITICAL(&s_playing_lock);

    id[0] = '\0';
    if (!playing) return false;
    const int written = snprintf(id, id_size, "%s", copy);
    if (written < 0 || (size_t)written >= id_size) {
        id[0] = '\0';
        return false;
    }
    if (liked != NULL) *liked = mark;
    if (disliked != NULL) *disliked = against;
    return true;
}

bool yandex_rotor_playing_cover_url(char *url, size_t url_size)
{
    if (url == NULL || url_size == 0U) return false;
    char uri[YANDEX_TRACK_COVER_MAX + 1U];
    bool playing;
    taskENTER_CRITICAL(&s_playing_lock);
    playing = s_rotor.playing;
    memcpy(uri, s_rotor.playing_cover, sizeof(uri));
    taskEXIT_CRITICAL(&s_playing_lock);

    url[0] = '\0';
    if (!playing) return false;
    return yandex_cover_url_web(uri, url, url_size);
}

void yandex_rotor_set_playing_liked(bool liked)
{
    taskENTER_CRITICAL(&s_playing_lock);
    s_rotor.playing_liked = liked;
    /* Liking a disliked track takes it out of the dislikes server-side -
     * measured - so the mark here follows rather than being left to disagree
     * with the account until the next track. */
    if (liked) s_rotor.playing_disliked = false;
    taskEXIT_CRITICAL(&s_playing_lock);
}

void yandex_rotor_set_playing_disliked(bool disliked)
{
    taskENTER_CRITICAL(&s_playing_lock);
    s_rotor.playing_disliked = disliked;
    if (disliked) s_rotor.playing_liked = false;
    taskEXIT_CRITICAL(&s_playing_lock);
}

void yandex_rotor_note_skip(void)
{
    s_rotor.skipped = true;
}

void yandex_rotor_stop(void)
{
    /* Before the buffer goes: a source switched off half-way through a track
     * still listened to that much of it, and saying so is the difference
     * between a heard track and one Yandex may offer again tomorrow. */
    yandex_rotor_report_end();
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

    /* First, and before the batch can be replaced: the track being left belongs
     * to the batch that is still current here. */
    yandex_rotor_report_end();

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

    /* Only once the link is in hand: a track that could not be resolved was
     * never played, and reporting it as started would teach the rotor that it
     * was heard. */
    s_rotor.playing_since_us = esp_timer_get_time();
    s_rotor.playing_duration_ms = candidate.duration_ms;
    snprintf(s_rotor.playing_batch, sizeof(s_rotor.playing_batch), "%s",
             s_rotor.batch.batch_id);
    /* Formatted first and published second: the copy under the lock has to be
     * a whole id, and snprintf into the shared array would let a reader in
     * half way through it. The mark comes from the rotor's own answer, which
     * says for every track it hands out whether the account has it liked. */
    char playing_id[YANDEX_TRACK_ID_MAX + 1U] = {0};
    snprintf(playing_id, sizeof(playing_id), "%s", candidate.id);
    char playing_cover[YANDEX_TRACK_COVER_MAX + 1U] = {0};
    snprintf(playing_cover, sizeof(playing_cover), "%s", candidate.cover);
    taskENTER_CRITICAL(&s_playing_lock);
    s_rotor.playing = true;
    s_rotor.playing_liked = candidate.liked;
    /* Nothing to read it from: the rotor reports the like and not the dislike.
     * False is also the truthful answer nearly always, since a track the
     * account has rejected is one this station stops offering. */
    s_rotor.playing_disliked = false;
    memcpy(s_rotor.playing_id, playing_id, sizeof(playing_id));
    memcpy(s_rotor.playing_cover, playing_cover, sizeof(playing_cover));
    taskEXIT_CRITICAL(&s_playing_lock);

    yandex_feedback_event_t event;
    yandex_rotor_event_init(&event, YANDEX_FEEDBACK_TRACK_STARTED);
    snprintf(event.track_id, sizeof(event.track_id), "%s", candidate.id);
    snprintf(event.batch_id, sizeof(event.batch_id), "%s", s_rotor.batch.batch_id);
    (void)yandex_feedback_post(&event);

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

const char *yandex_rotor_current_url(void)
{
    if (s_rotor.scratch == NULL) return NULL;
    char id[YANDEX_TRACK_ID_MAX + 1U];
    taskENTER_CRITICAL(&s_playing_lock);
    const bool playing = s_rotor.playing;
    memcpy(id, s_rotor.playing_id, sizeof(id));
    taskEXIT_CRITICAL(&s_playing_lock);
    if (!playing || id[0] == '\0') return NULL;

    /* Only the link is fetched again. The batch does not move, no start or end
     * is reported, and the cover is not republished: this is the same track,
     * still on the air, and everything that says "a track began" has already
     * been said about it. Reporting it twice would teach the station that it
     * was played twice.
     *
     * last_error is left alone for the same reason - it is what the screen
     * shows about getting music out of the station, and a link that would not
     * re-sign is not that. */
    if (yandex_rotor_resolve(id, s_rotor.url, YANDEX_LINK_URL_MAX + 1U) != ESP_OK) {
        ESP_LOGW(TAG, "could not sign %s again", id);
        return NULL;
    }
    return s_rotor.url;
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
