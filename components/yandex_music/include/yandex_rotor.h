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
 * That alone is enough to advance it within one session, but it tells Yandex
 * nothing: what was heard, skipped or played to the end is reported separately
 * through yandex_feedback, which is what stops a station opened tomorrow from
 * beginning where it began today.
 *
 * Every call here blocks and performs a TLS handshake, so all of them belong
 * on a task with room for one - and none of them may run on the HTTP server
 * task, which would stall the web UI for the duration. */

/* Begins a chain. Allocates the response buffer, so it has a matching stop
 * even if no track is ever fetched. Starting a chain that is already running
 * on the same station keeps its position; a different station resets it.
 *
 * `from` is the station's idForFrom, reported to the rotor as the place the
 * listening started; NULL or empty is accepted, and so it is by the API. */
esp_err_t yandex_rotor_start(const char *station_id, const char *from);

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

/* A fresh link to the track already on the air, for reopening it where a pause
 * left it. NULL when nothing is playing or the link will not sign again.
 *
 * Matches internet_radio_track_reopen_fn. It carries no title because there is
 * no new track to name - the one on the screen is still the right one - and it
 * reports nothing to the station: a track resumed was not played twice. */
const char *yandex_rotor_current_url(void);

/* Records that the listener asked for the next track rather than letting this
 * one finish, so that the track being left is reported as a skip. Consumed by
 * the next track change; harmless when nothing is playing.
 *
 * Called by player_control rather than deduced from how much was played: a
 * short play is also what a failed stream looks like, and telling the rotor
 * "the listener rejected this" when the network dropped would teach it the
 * wrong thing. */
void yandex_rotor_note_skip(void);

/* The track on the air and which of the two marks the account has on it, for
 * the heart on the screen and in the web UI. False when nothing is playing.
 * Either output pointer may be NULL.
 *
 * Copied out under a spinlock rather than read field by field: the chain is
 * advanced by the decode task, and a snapshot is built on whichever task asks
 * for one - so without it a reader could take half of one track's id and half
 * of the next one's. */
bool yandex_rotor_playing_track(char *id, size_t id_size, bool *liked, bool *disliked);

/* Records the mark the account now carries for the playing track, once the API
 * has accepted it. Kept here rather than in player_control because this is
 * where the track itself lives: the next track that starts overwrites it, and
 * nothing has to remember to clear it.
 *
 * The two exclude each other, the way the API does: setting one clears the
 * other here as well, so the screen cannot show a heart that is filled and
 * struck through at once. */
/* The address of the track's cover at the size a browser wants, or false when
 * nothing is playing or the track came without a picture. The device does not
 * fetch this one: it hands the address to the page, which loads it itself and
 * so is not limited to the 96 px the panel decodes into. */
bool yandex_rotor_playing_cover_url(char *url, size_t url_size);

void yandex_rotor_set_playing_liked(bool liked);
void yandex_rotor_set_playing_disliked(bool disliked);

/* Why the last attempt failed, for the message the user sees.
 * ESP_ERR_NOT_SUPPORTED means the account has no active subscription. */
esp_err_t yandex_rotor_last_error(void);
#endif
