#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Room for any URL a media server hands out.
 *
 * The ones measured on this LAN are short - a control URL is about 60
 * characters, a track's is about 55 - but a server that puts a session token
 * in a query string is ordinary, and a URL that does not fit is a track that
 * cannot be played. */
#define DLNA_URL_MAX 256U

/* Turns the reference a server wrote into one the device can fetch.
 *
 * A UPnP device description names its services with a *relative* URL - the
 * server on this LAN answers `/ContentDirectory/<uuid>/control.xml` - and the
 * base it is relative to is the address the description itself came from. Both
 * halves are known only at that moment, so joining them is not a detail that
 * can be skipped: without it the device has a service it cannot address.
 *
 * Handles the three shapes that occur: already absolute, rooted at the host
 * ("/x/y"), and relative to the base's directory ("y"). False when the result
 * would not fit, or when `base` names no host - never a truncated URL, which
 * would be a request to somewhere else. */
bool dlna_url_resolve(const char *base, const char *reference, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
