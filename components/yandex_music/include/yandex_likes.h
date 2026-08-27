#pragma once

#include <stdbool.h>
#include <stddef.h>

/* The two marks a track can carry in the account's own library.
 *
 * Reading the like costs nothing: the rotor's answer already says whether each
 * track it hands out is liked, and yandex_tracks_parse_batch() carries that
 * through to yandex_track_t. **The dislike is not reported there** - measured
 * 2026-08-26, a sequence item carries only `liked`, `track`, `trackParameters`
 * and `type` - so a dislike is known only for as long as the listener's own
 * press is remembered. That is enough in practice, because a disliked track is
 * what the rotor stops handing out.
 *
 * Measured contract, 2026-08-24 for likes and 2026-08-26 for dislikes, against
 * api.music.yandex.net:
 *   POST /users/{uid}/likes/tracks/add?track-id={id}
 *   POST /users/{uid}/likes/tracks/remove?track-ids={id}
 *   POST /users/{uid}/dislikes/tracks/add?track-id={id}
 *   POST /users/{uid}/dislikes/tracks/remove?track-ids={id}
 *   - the endpoints disagree about the parameter's name, and each refuses the
 *     other's with HTTP 400 "Parameters requirements are not met". Singular to
 *     add, plural to remove; that is the API, not a typo here, and the dislike
 *     pair repeats it exactly.
 *   - no body is needed, and none is sent.
 *   - the answer is {"result":{"revision":N}}, about 190 bytes; a refusal is
 *     about 280.
 *   - measured 80-105 ms from a PC for likes, 300-345 ms for dislikes.
 *   - **the two marks exclude each other server-side, in both directions**:
 *     disliking a liked track drops it out of the likes, and liking a disliked
 *     one drops it out of the dislikes. Verified by reading both library lists
 *     around each call, so this end never has to send a second request to undo
 *     the other mark.
 *
 * The account id in the path is not the token's - it has to be asked for
 * separately, with GET /account/status. */

typedef enum {
    YANDEX_MARK_LIKE = 0,
    YANDEX_MARK_DISLIKE,
} yandex_mark_t;

/* Builds the path for setting or clearing one mark on `track_id`. Returns its
 * length, or 0 when either id is not a decimal number or the path will not fit.
 *
 * Both ids are refused rather than escaped when they hold anything else: they
 * come from the API's own answers, so anything but digits means the answer was
 * not what we take it for, and the right response is to send nothing at all. */
size_t yandex_likes_path(const char *uid, const char *track_id, yandex_mark_t mark, bool set,
                         char *out, size_t out_size);

/* Room for a decimal account id and its terminator. Yandex hands out both the
 * short nine-digit ids and the sixteen-digit ones registered by phone, so this
 * is sized for the long form and the id is kept as text: it is only ever put
 * back into a URL, and a 64-bit parse would be a conversion for nobody. */
#define YANDEX_UID_MAX 23U

/* Reads the account id out of a GET /account/status answer (result.account.uid,
 * a JSON number). False when it is absent or will not fit. */
bool yandex_account_parse_uid(const char *json, char *out, size_t out_size);

#ifdef ESP_PLATFORM
#include "esp_err.h"

/* Sets or clears one mark on the track, and returns only once the API has
 * answered.
 *
 * Blocking, with a TLS handshake inside it: the caller must be a task with the
 * stack for one. player_control is, and is where this is called from - the
 * same task that already blocks for three such calls to open a station.
 *
 * The account id is fetched on the first call and kept, so only the first
 * press after a restart pays for the extra request. */
esp_err_t yandex_likes_set(const char *track_id, yandex_mark_t mark, bool set);

/* Forgets the cached account id. Called when the account is unlinked, so that
 * a different account cannot be handed the previous one's id. */
void yandex_likes_forget(void);
#endif
