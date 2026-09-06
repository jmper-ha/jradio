#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dlna_device.h"
#include "dlna_ssdp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Finding the media servers on the LAN.
 *
 * A search is a multicast datagram and a couple of seconds of listening, so it
 * blocks - it is not something to run from the UI task while a screen is
 * waiting to be drawn. The caller owns the array, which is small enough to
 * live on a task's stack only if that task has room for it; the browser keeps
 * one. */

/* How many servers a home network is expected to hold. More than one is
 * already unusual - a NAS, and perhaps a media player on a PC - and a device
 * with an encoder and a list is not where anybody wants to scroll through
 * twelve of them. Servers past this many are ignored, not queued. */
#define DLNA_DISCOVERY_SERVER_MAX 4U

typedef struct {
    /* The identity a server announces. Two answers carrying the same one are
     * one server, which is not hypothetical: the server here replies twice to
     * every search. */
    char usn[DLNA_SSDP_USN_MAX];
    char location[DLNA_URL_MAX];
    dlna_device_t device;
} dlna_server_t;

/* Searches, then fetches each answer's description, and returns how many
 * servers came back that this device can actually browse.
 *
 * Blocks for `listen_ms` plus however long the descriptions take. Servers that
 * answer and turn out to offer no ContentDirectory are dropped here rather
 * than shown and then found useless when they are opened. */
size_t dlna_discovery_search(dlna_server_t *servers, size_t capacity, uint32_t listen_ms);

#ifdef __cplusplus
}
#endif
