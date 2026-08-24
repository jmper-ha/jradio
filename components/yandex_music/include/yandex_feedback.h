#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "yandex_catalog.h"
#include "yandex_track.h"

/* What the device tells the rotor it did with the tracks it was given.
 *
 * Not decoration: the rotor keeps its idea of what this account has already
 * heard on the server, and `?queue=<previous track>` only moves the chain
 * along inside one session. Without these events a station opened tomorrow
 * starts from the same place it started today, because nothing ever told
 * Yandex the songs were played - which is what repeats sound like.
 *
 * Measured contract, 2026-08-24, against api.music.yandex.net:
 *   POST /rotor/station/{id}/feedback[?batch-id=...]
 *   - the body must be JSON. Form-encoded is refused with HTTP 400
 *     "condition is not met" whatever the fields say.
 *   - `timestamp` is required (400 without it); an integer is accepted, and so
 *     is 0 - so an unsynchronised clock delays nothing.
 *   - `trackId` is required for everything except radioStarted.
 *   - `totalPlayedSeconds` is required for trackFinished and skip.
 *   - `from` on radioStarted and the batch-id query are both optional.
 *   - an unknown `type` is refused with HTTP 400.
 * Each POST measured 90-240 ms from a PC. */

typedef enum {
    /* The station was opened. */
    YANDEX_FEEDBACK_RADIO_STARTED = 0,
    /* A track began playing. */
    YANDEX_FEEDBACK_TRACK_STARTED,
    /* A track ran to its end, or the source was stopped part-way through it. */
    YANDEX_FEEDBACK_TRACK_FINISHED,
    /* The listener asked for the next track instead. */
    YANDEX_FEEDBACK_SKIP,
} yandex_feedback_kind_t;

/* Self-contained on purpose: an event is sent seconds after it is recorded,
 * and by then the station, the batch or the track may all have moved on.
 * Carrying a copy is a few hundred bytes; carrying a pointer into the rotor
 * would be a report filed against the wrong station. */
typedef struct {
    yandex_feedback_kind_t kind;
    char station[YANDEX_STATION_ID_MAX + 1];
    /* idForFrom, for radioStarted only. Empty is accepted by the API. */
    char from[YANDEX_STATION_FROM_MAX + 1];
    char batch_id[YANDEX_TRACK_BATCH_ID_MAX + 1U];
    char track_id[YANDEX_TRACK_ID_MAX + 1U];
    uint32_t played_ms;
    /* Unix seconds, 0 when the clock has not been set yet. Filled in by
     * yandex_feedback_post() at the moment the event is recorded; a caller
     * sets it only in a test. */
    uint32_t timestamp;
} yandex_feedback_event_t;

/* Builds the JSON body for `event`. Returns its length, or 0 when the event
 * makes no sense (a track event with no track) or will not fit. */
size_t yandex_feedback_body(const yandex_feedback_event_t *event, char *out, size_t out_size);

/* Builds the request path, with ?batch-id= when the event carries one.
 * Returns its length, or 0 when there is no station or it will not fit. */
size_t yandex_feedback_path(const yandex_feedback_event_t *event, char *out, size_t out_size);

#ifdef ESP_PLATFORM
#include "esp_err.h"

/* Creates the lock the queue is built under. Costs a mutex and nothing else:
 * the queue and the sending task are both created on the first real event, so
 * a device that never opens a Yandex station pays for none of it. */
esp_err_t yandex_feedback_init(void);

/* Files one event. Returns at once: the POST is a TLS handshake, and every
 * caller here is either the decode task in the gap between two tracks or
 * player_control, neither of which can afford one.
 *
 * Best effort by design. A full queue drops the event with a warning rather
 * than blocking a caller, because a listening report that arrives late is
 * worth nothing and a stalled decoder is worth less than nothing. */
esp_err_t yandex_feedback_post(const yandex_feedback_event_t *event);
#endif
