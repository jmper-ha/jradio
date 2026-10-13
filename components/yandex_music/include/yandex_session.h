#pragma once

#include <stddef.h>

#include "yandex_track.h"

/* The requests of a rotor session - the way yandex-music-api 3.x and the
 * official clients play a station now, in place of the bare
 * /rotor/station/{id}/tracks?queue= chain.
 *
 * Measured contract, 2026-10-13, against api.music.yandex.net with the
 * Android client header:
 *   POST /rotor/session/new {"seeds":[...],"includeTracksInResponse":true}
 *     - answers radioSessionId, batchId and a "sequence" of five tracks, about
 *       15 KB; a station id ("user:onyourwave", "genre:rock", "artist:36800")
 *       is a seed as it is, and "settingDiversity:discover" and the like may
 *       sit beside it.
 *   POST /rotor/session/{id}/tracks {"queue":[track ids already handed out]}
 *     - the next five, with a new batchId.
 *     - an expired id is not an error: HTTP 200, "unknownSession":true and
 *       five stand-in tracks.
 *   POST /rotor/session/{id}/feedback - see yandex_feedback.h.
 * Each call measured 0.5-0.85 s from a PC. */

/* The body that opens a session on `station`. Returns its length, or 0 when
 * the station is not an identifier or the buffer will not hold it. */
size_t yandex_session_new_body(const char *station, char *out, size_t out_size);

/* The path that asks session `session_id` for its next batch. 0 when the id
 * is not one, or will not fit - never a truncated path. */
size_t yandex_session_tracks_path(const char *session_id, char *out, size_t out_size);

/* The body of that request: every track of `batch`, the one just handed out,
 * so that none of them comes back. A track id that is not an identifier is
 * left out rather than costing the request. */
size_t yandex_session_tracks_body(const yandex_track_batch_t *batch, char *out, size_t out_size);
