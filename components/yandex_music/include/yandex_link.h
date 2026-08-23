#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Turning a track id into something the audio path can open.
 *
 * It takes three requests and a signature, and none of it is guesswork - the
 * chain below was measured against the live API on 2026-08-23:
 *
 *   GET /tracks/{id}/download-info   -> variants, each with a downloadInfoUrl
 *   GET <downloadInfoUrl>            -> XML carrying host, path, ts and s
 *   sign = md5("XGRlBW9FXlekgbPrRHuSiA" + path-without-slash + s)
 *   https://{host}/get-mp3/{sign}/{ts}{path}
 *
 * The result is a plain MP3 stream with no DRM and no ID3 tag, and the link
 * stops working after about a minute - so it is fetched when the track is
 * about to play, never stored.
 *
 * All three hosts measured as api.music.yandex.net, which is why the audio
 * fetch needs no second certificate chain and no second TLS session. */

/* The downloadInfoUrl measured 1083 characters and the signed link 1108, both
 * of them mostly one opaque path segment. Sized with room to spare because a
 * clipped URL fetches nothing and says nothing about why. */
#define YANDEX_LINK_URL_MAX 1279U

typedef enum {
    YANDEX_LINK_OK = 0,
    /* The answer is not shaped the way the API documented itself in the
     * capture: a changed format, an error body, or plain truncation. */
    YANDEX_LINK_ERR_FORMAT,
    /* Every variant is a 30-second preview. This is exactly how the API
     * answers an account without an active subscription, so it earns its own
     * result: the user needs to be told that, not "playback failed". */
    YANDEX_LINK_ERR_PREVIEW_ONLY,
    /* A field did not fit. Separate from ERR_FORMAT because it means our
     * limits are wrong, not the server's answer. */
    YANDEX_LINK_ERR_TOO_LONG,
} yandex_link_result_t;

/* Picks the variant to play out of a /tracks/{id}/download-info answer and
 * copies its downloadInfoUrl. Accepts the answer with or without the "result"
 * envelope.
 *
 * Highest-bitrate MP3 that is not a preview. Only MP3: it is what the endpoint
 * is named after, what the account was measured to return, and what the
 * decoder is known to handle for these files. */
yandex_link_result_t yandex_link_pick_variant(const char *json, char *url, size_t url_size,
                                              uint16_t *bitrate_kbps);

/* Builds the signed MP3 URL from the XML the picked variant points at. */
yandex_link_result_t yandex_link_from_xml(const char *xml, char *url, size_t url_size);

/* The cover size asked for. The player screen's tile is 96x96 and the service
 * offers 100x100, which measured 4.3 KB against 11.8 KB for 200x200 - the
 * larger picture would only be thrown away by the scaler. */
#define YANDEX_COVER_SIZE_TAG "100x100"
#define YANDEX_COVER_URL_MAX 191U

/* Turns a track's coverUri - a host and path with "%%" standing in for a size -
 * into a URL that can be fetched.
 *
 * Plain HTTP on purpose. The picture is public and carries no token, and
 * skipping TLS keeps this fetch off the internal SRAM that the audio stream's
 * own TLS session needs; it also measured 40 ms against 147 ms.
 *
 * False when there is no size marker or the result would not fit, so a cover
 * is dropped rather than fetched from a wrong address. */
bool yandex_cover_url(const char *cover_uri, char *url, size_t url_size);
