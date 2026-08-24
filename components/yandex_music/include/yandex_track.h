#pragma once

#include <stdbool.h>
#include <stdint.h>

/* One batch of tracks as the rotor hands them out.
 *
 * A station is not a stream: GET /rotor/station/{id}/tracks answers with a
 * short list, and the chain moves on by asking again with ?queue=<id of the
 * track just played>. Measured 2026-08-23, the answer holds five tracks and
 * runs 12 KB - which is why nothing here keeps the document, only the handful
 * of fields a player and a screen actually need. */

/* Track ids are decimal and short today (8 digits), but they are handed back
 * to the server in a URL, so the field is sized for growth and never clipped. */
#define YANDEX_TRACK_ID_MAX 23U
/* Long enough for a Russian title in UTF-8 - roughly 60 Cyrillic letters,
 * about twice what fits across the display. */
#define YANDEX_TRACK_TITLE_MAX 127U
/* "feat. Haylen; Wolfgang Lohr Remix" and the like: part of the name in every
 * Yandex client, but a separate field in the answer. */
#define YANDEX_TRACK_VERSION_MAX 63U
/* All performers joined with ", ", the way the track is credited. */
#define YANDEX_TRACK_ARTIST_MAX 127U
/* The cover as the answer gives it: a host and path with "%%" where a size
 * belongs, about 70 characters. */
#define YANDEX_TRACK_COVER_MAX 127U
/* Five is what the rotor returns; the extra room means a larger batch is
 * carried rather than rejected. */
#define YANDEX_TRACK_BATCH_MAX 8U
/* The batchId the rotor stamps on the answer, measured at 42 characters
 * ("1787591264830687-16297747482082378090.p6O1"). It is handed back to the
 * feedback endpoint so an event is attributed to the batch it belongs to; the
 * API accepts feedback without it, so a batch id that does not fit is dropped
 * rather than allowed to cost the tracks. */
#define YANDEX_TRACK_BATCH_ID_MAX 63U

typedef struct {
    char id[YANDEX_TRACK_ID_MAX + 1U];
    char title[YANDEX_TRACK_TITLE_MAX + 1U];
    char version[YANDEX_TRACK_VERSION_MAX + 1U];
    char artist[YANDEX_TRACK_ARTIST_MAX + 1U];
    char cover[YANDEX_TRACK_COVER_MAX + 1U];
    uint32_t duration_ms;
    bool liked;
} yandex_track_t;

typedef struct {
    uint8_t count;
    char batch_id[YANDEX_TRACK_BATCH_ID_MAX + 1U];
    yandex_track_t tracks[YANDEX_TRACK_BATCH_MAX];
} yandex_track_batch_t;

/* Parses one answer of /rotor/station/{id}/tracks. Accepts it with or without
 * the "result" envelope, so a test fixture need not carry one.
 *
 * Tracks the answer marks unavailable are left out: they have no download link
 * behind them, so passing one on would only turn into a failed fetch later.
 * A track that cannot be read at all is skipped without costing the others -
 * an unplayable batch is a silent radio, a batch short one track is not.
 *
 * Returns false only when no usable track came out of it. */
bool yandex_tracks_parse_batch(const char *json, yandex_track_batch_t *batch);
