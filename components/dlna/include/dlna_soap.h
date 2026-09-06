#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The one request this device makes of a media server: Browse.
 *
 * ContentDirectory has a dozen actions and the device needs exactly one of
 * them - list the children of an object - so this builds that request and
 * finds the payload in the answer, and knows nothing else about SOAP.
 *
 * The answer is peculiar in a way worth stating: the listing comes back as a
 * whole XML document *escaped into a text field* of the reply, so it is read
 * out in two passes - unwrap the envelope here, then unescape and parse the
 * DIDL. That is why this returns where the payload is rather than a copy of
 * it: the device unescapes it inside the buffer it already read, and a second
 * 4 KB buffer for a browse is 4 KB it does not have. */

#define DLNA_SOAP_CONTENT_TYPE "text/xml; charset=\"utf-8\""
#define DLNA_SOAP_ACTION_BROWSE \
    "\"urn:schemas-upnp-org:service:ContentDirectory:1#Browse\""

/* How many entries to ask for at once.
 *
 * Not a page of the screen: a round trip costs a request, an answer and the
 * time to parse it, so asking for one screenful at a time would fetch the same
 * container several times just to scroll through it.
 *
 * The number comes from measuring the real library on this LAN, and the first
 * guess at it was wrong by a factor of four. An entry is not a fixed size and
 * the spread is large:
 *
 *     a track          ~1.2 KB
 *     an album         ~1.8 KB   (long titles, non-Latin)
 *     an artist        ~3.7 KB   (the most a container carries here)
 *
 * so 25 artists came back as 110 KB and overran a 32 KB buffer, which reads as
 * a container that will not open. Eight of the largest kind is about 30 KB,
 * which fits DLNA_CLIENT_RESPONSE_MAX with room for a server more verbose than
 * this one.
 *
 * It is a starting point rather than a promise: nothing bounds how much a
 * server chooses to say about an object, so an answer that fills the buffer is
 * detected and the page halved. See dlna_client_browse(). */
#define DLNA_SOAP_BROWSE_PAGE 8U

/* Writes the Browse request body for the children of `object_id` ("0" is the
 * root). Returns the length to send, or 0 when it does not fit - a truncated
 * envelope is a request the server rejects, so the caller has to be able to
 * tell. */
size_t dlna_soap_build_browse(char *out, size_t out_size, const char *object_id,
                              size_t starting_index, size_t requested_count);

typedef struct {
    /* What this answer holds, and what the container holds altogether. The
     * second is how the caller knows whether to ask again: a container of 38
     * albums comes back 25 at a time. */
    size_t number_returned;
    size_t total_matches;
    /* Where the escaped DIDL sits in the response buffer. Offsets rather than
     * pointers so the caller can unescape it in place. */
    size_t result_offset;
    size_t result_length;
} dlna_soap_browse_t;

/* Unwraps a Browse reply. False for a reply carrying no Result at all,
 * including a SOAP fault - see dlna_soap_fault_code() for saying which. */
bool dlna_soap_parse_browse(const char *xml, size_t length, dlna_soap_browse_t *out);

/* The UPnP error behind a fault, or 0 when the reply is not one.
 *
 * Worth telling apart in the log: 701 is "no such object", which is a stale id
 * after the server's library was re-scanned and means re-browse from the root,
 * while 401 is an action the server does not have and means this server will
 * never answer. Retrying is right for one and pointless for the other. */
int dlna_soap_fault_code(const char *xml, size_t length);

#ifdef __cplusplus
}
#endif
