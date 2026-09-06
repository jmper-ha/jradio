#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dlna_url.h"
#include "radio_stream_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One page of a server's listing, turned into rows the device can show.
 *
 * DIDL-Lite is what a Browse answers with: a flat list of containers (things
 * to open) and items (things to play), each carrying its title, whatever tags
 * the server holds, and for an item one or more <res> elements naming a URL
 * and what is on the end of it.
 *
 * The decoder decides what "playable" means, so this reuses the radio's format
 * enum rather than declaring a second one that would have to be mapped: a
 * media server hands out an HTTP URL with a Content-Type, which is exactly
 * what internet_radio already opens, and the mapping from a MIME type to a
 * decoder already exists there. The measured server offers
 * `http-get:*:audio/mpeg:...` for a 320 kbit/s MP3 - a stream this device has
 * been playing since the first day.
 *
 * An entry is around 840 bytes and a page of them is some 20 KB, so the array
 * belongs in PSRAM like every other buffer of that size on this board. */

/* Object ids are the server's, and opaque: 20 hex characters for a library
 * object here, a 36-character UUID for the server's own folders. Kept whole
 * and never parsed - a truncated id names a different object, or none. */
#define DLNA_OBJECT_ID_MAX 64U

/* A title as it goes on a row. The longest album name on this server is 100
 * bytes, and Japanese track titles run to about 60 - UTF-8, so a "character"
 * here can be three bytes. */
#define DLNA_TITLE_MAX 128U
#define DLNA_TEXT_MAX 64U

/* The root of every server's tree. Not a value the device invents: "0" is what
 * ContentDirectory defines as the root object, and it is where a browse
 * starts. */
#define DLNA_OBJECT_ID_ROOT "0"

typedef enum {
    /* Something to open. */
    DLNA_ENTRY_CONTAINER = 0,
    /* Something to play, if we can decode it. */
    DLNA_ENTRY_ITEM,
} dlna_entry_kind_t;

typedef struct {
    dlna_entry_kind_t kind;
    char id[DLNA_OBJECT_ID_MAX];
    char title[DLNA_TITLE_MAX];
    char artist[DLNA_TEXT_MAX];
    char album[DLNA_TEXT_MAX];
    /* Items only. Plain HTTP on the server measured, which is the whole reason
     * this source is cheap: no TLS, and so none of the internal-SRAM trouble
     * that HTTPS radio streams run into. */
    char url[DLNA_URL_MAX];
    char art_url[DLNA_URL_MAX];
    uint32_t duration_ms;
    /* Meaningful only when `playable`. */
    radio_stream_format_t format;
    /* An item this build can decode, with a URL to fetch it from. False for a
     * video, for a codec that is not built in, and for an audio item the
     * server described without a usable <res> - all of which are rows to show
     * greyed out rather than rows to hide, because a listing that silently
     * drops what it cannot play looks like a server with missing files. */
    bool playable;
} dlna_entry_t;

/* Reads a DIDL-Lite document, already unescaped, into `entries`.
 *
 * Returns how many rows were written. `total_seen` counts every container and
 * item in the document including the ones that did not fit, so a caller can
 * tell a short page from a full one.
 *
 * Rows come back in the order the server wrote them. That order is the one
 * thing a listing must not rearrange: a server sorts an album by track number,
 * and sorting it again by title would play it wrong. */
size_t dlna_didl_parse(const char *didl, size_t length, dlna_entry_t *entries,
                       size_t capacity, size_t *total_seen);

#ifdef __cplusplus
}
#endif
