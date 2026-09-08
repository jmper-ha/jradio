#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "dlna_url.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Finding a media server: the half with no socket in it.
 *
 * SSDP is HTTP-shaped text over UDP multicast. The device sends one search and
 * reads whatever datagrams come back over the next couple of seconds; both
 * ends of that are a few lines of text, which is all this file is. The socket
 * belongs to the device build.
 *
 * Measured against the server on this LAN: it answers a single search twice,
 * with identical USNs. So the identity of a server is its USN and de-duplicating
 * on it is not optional - without it one server is two rows on the screen. */

#define DLNA_SSDP_MULTICAST_ADDRESS "239.255.255.250"
#define DLNA_SSDP_PORT 1900U

/* What the device searches for. Not `ssdp:all`: on a home network that brings
 * back printers, routers and light bulbs, and every one of them would have to
 * be fetched and rejected. */
#define DLNA_SSDP_SEARCH_TARGET "urn:schemas-upnp-org:device:MediaServer:1"

/* A USN is a UUID plus the device type it announces, so about 90 characters.
 * Kept whole because it is only ever compared, never shown. */
#define DLNA_SSDP_USN_MAX 128U

typedef struct {
    /* Where the device description is. The one thing a search is for. */
    char location[DLNA_URL_MAX];
    char usn[DLNA_SSDP_USN_MAX];
} dlna_ssdp_response_t;

/* Writes the M-SEARCH datagram. `mx_seconds` is how long a server may wait
 * before answering, which spreads replies out so a busy network does not
 * deliver them all in one burst; 2 is what the search on this LAN used.
 * Returns the length to send, or 0 when it does not fit. */
size_t dlna_ssdp_build_search(char *out, size_t out_size, unsigned mx_seconds);

/* Reads one datagram.
 *
 * True only for a datagram that announces a media server and says where its
 * description is - a reply to a search, or the same server announcing itself
 * unasked. A server saying goodbye is refused, as is any other device that
 * happens to be shouting on the group: both would otherwise become a row the
 * user can select and nothing behind it. */
bool dlna_ssdp_parse_response(const char *data, size_t length, dlna_ssdp_response_t *out);

/* The UUID out of a USN: "uuid:4d696e69-…::urn:schemas-upnp-org:device:…" is
 * one server announcing one device type, and everything after the "::" says
 * which type rather than which server.
 *
 * Written down between runs as the name of a server to come back to - a resume
 * point cannot be the row a server sat on, because the order servers answer a
 * search in is the order they happened to reply. The type suffix is dropped
 * because it is the same on every media server and would spend a third of the
 * settings line saying so.
 *
 * False when there is no uuid to take, or when it does not fit `out_size`. */
#define DLNA_SSDP_UDN_MAX 64U
bool dlna_ssdp_udn(const char *usn, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
