#pragma once

#include "yandex_catalog.h"
#include "yandex_link.h"
#include "yandex_track.h"

#ifdef ESP_PLATFORM
#include "esp_err.h"

/* One station, played as an endless chain of tracks.
 *
 * The rotor hands out a short batch at a time - five tracks, measured - and
 * the chain moves on by asking again with ?queue=<the track just played>.
 * Nothing else is needed to advance it: the feedback endpoint that would tell
 * Yandex what was listened to is deliberately not used, so the station never
 * learns from this device (and the dashboard's recommendations are shaped by
 * the user's other players only).
 *
 * Every call here blocks and performs a TLS handshake, so all of them belong
 * on a task with room for one - and none of them may run on the HTTP server
 * task, which would stall the web UI for the duration. */

/* Begins a chain. Allocates the response buffer, so it has a matching stop
 * even if no track is ever fetched. Starting a chain that is already running
 * on the same station keeps its position; a different station resets it. */
esp_err_t yandex_rotor_start(const char *station_id);

/* Releases the buffer. Safe to call when nothing was started. */
void yandex_rotor_stop(void);

/* Resolves the next track of the chain and fills `url` with a link the audio
 * path can open. `track` receives what to put on the screen.
 *
 * The link expires in about a minute, which is why this is called when the
 * track is about to play rather than in advance.
 *
 * Returns ESP_ERR_NOT_SUPPORTED when the account has no active subscription -
 * the API answers such an account with preview variants only, and that needs
 * saying out loud rather than looking like a network failure.
 * ESP_ERR_INVALID_STATE means no account is linked, ESP_ERR_NOT_FOUND that the
 * station returned nothing playable. */
esp_err_t yandex_rotor_next(char *url, size_t url_size, yandex_track_t *track);

/* The shape internet_radio asks for: the next link, with the line to display
 * while it plays, or NULL when the chain cannot go on. The returned pointer
 * stays valid until the next call.
 *
 * Matches internet_radio_track_source_fn, and is registered with
 * internet_radio_set_track_source() rather than called directly, so that the
 * audio path keeps knowing nothing about Yandex. */
const char *yandex_rotor_next_url(char *title, size_t title_size);

/* Why the last attempt failed, for the message the user sees.
 * ESP_ERR_NOT_SUPPORTED means the account has no active subscription. */
esp_err_t yandex_rotor_last_error(void);
#endif
