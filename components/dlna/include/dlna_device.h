#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "dlna_url.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What the device needs out of a server's description, and nothing else.
 *
 * A search says where the description is; the description says what the server
 * is called and where to send a Browse. Those two answers are the whole
 * purpose of fetching it - the icons, the model numbers and the vendor
 * extensions are read past.
 *
 * The server on this LAN calls itself "Plex Media Server: nas4free" and gives
 * its control URL *relative* to the description's address, which is why
 * dlna_url_resolve() is in the path rather than a straight copy. */

/* The name a server calls itself, as it goes on the screen. The one on this
 * LAN needs 27 bytes; this holds a long one whole rather than showing half a
 * name, and a server that exceeds it is still usable - only its name is cut. */
#define DLNA_DEVICE_NAME_MAX 64U

typedef struct {
    char friendly_name[DLNA_DEVICE_NAME_MAX];
    /* Absolute, and ready to POST to. */
    char control_url[DLNA_URL_MAX];
} dlna_device_t;

/* Reads a device description fetched from `base_url`.
 *
 * False unless the document is a media server offering a ContentDirectory,
 * which is the only service this device can use: a description that parses but
 * offers no way to browse is not a server we can do anything with, and saying
 * so here keeps the "no content directory" case out of every caller.
 *
 * A missing friendlyName is not a failure - it is a server with no name, and
 * the caller shows its address instead. */
bool dlna_device_parse_description(const char *xml, size_t length, const char *base_url,
                                   dlna_device_t *out);

#ifdef __cplusplus
}
#endif
