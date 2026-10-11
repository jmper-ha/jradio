#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A station's picture fetched by the device on the page's behalf.
 *
 * The station search on the playlist page finds logos on the stations' own
 * sites, and most of those sites do not let a page read their pictures - the
 * browser shows one in an <img> but will not hand its pixels to a canvas, and
 * the canvas is where the picture is cut down to the 160 px the device keeps.
 * The device is not a browser and is not held to that, so the page asks it:
 * GET /api/icon-fetch?url=<the picture>. The first ask starts the fetch and is
 * answered 202; the page asks again with the same address until the answer is
 * the picture itself - from the device's own origin, which the canvas may
 * read - or an error: 413 too large, 415 not a picture, 502 the site would not
 * give it, 409 another address is being fetched.
 *
 * The rest of this header is the pure half - the query, the address and what
 * the bytes are - and is tested on the host. */

/* The longest address taken. A logo's address is rarely a fifth of this; one
 * longer is more likely a data: URL or something worse than a picture. */
#define WEB_ICON_FETCH_URL_MAX 512U
/* The largest picture fetched. A station's apple-touch-icon is tens of
 * kilobytes, and the page cuts any picture down anyway. Sites name worse as
 * their favicon - one in the catalogue's first page of Russian jazz is a
 * 1.37 MB PNG under a path saying 196x196 - and 2 MB was tried for it: it
 * came, in 28 seconds, and the playing stream ran dry twice on the way, for
 * about a second each. A missing logo is the smaller loss. One that says its
 * size is refused before a byte of it is read. In PSRAM, for one fetch. */
#define WEB_ICON_FETCH_BODY_MAX (512U * 1024U)

/* Percent-decodes one query value into `out`, '+' as a space, the way
 * encodeURIComponent()'s output and a form's both come back. False when it
 * does not fit or an escape is malformed. */
bool web_icon_fetch_decode(const char *in, char *out, size_t out_size);

/* Whether `url` is an address worth fetching: http or https, a host, nothing
 * that is not printable ASCII, and shorter than WEB_ICON_FETCH_URL_MAX. */
bool web_icon_fetch_url_ok(const char *url);

/* What a fetched body is, by its first bytes rather than by what the server
 * said: favicons come back as application/octet-stream or text/plain often
 * enough. The MIME type of a PNG, JPEG, GIF, WebP, ICO or SVG, or NULL for
 * anything else - a page of HTML where the picture was expected, mostly. */
const char *web_icon_fetch_sniff(const uint8_t *bytes, size_t length);

#ifdef ESP_PLATFORM

#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t web_icon_fetch_get(httpd_req_t *request);

#endif
