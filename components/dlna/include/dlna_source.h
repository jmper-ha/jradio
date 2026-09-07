#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#include "dlna_didl.h"
#include "dlna_discovery.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The DLNA source: which server, where in its tree, and what is on screen.
 *
 * Shaped after file_storage, because it is the same job. One listing is open
 * at a time and everything above reads it - the browser draws it, the track
 * keys step through it, and the chain of tracks being played walks it. A
 * second copy would cost as much as the first and answer no question the first
 * one cannot.
 *
 * It follows from that, exactly as it does for the USB drive, that browsing
 * away while something is playing moves what "next track" means. That is not
 * an oversight: on a device with one screen, the listing on the screen is the
 * one the user is thinking about.
 *
 * Nothing is held while the source is closed. A search, a page of a listing
 * and the parsed rows are some 130 KB between them - all PSRAM, none of it
 * worth keeping allocated while the device is playing the radio - so this is
 * opened when the source is selected and released when it is stopped, the same
 * discipline the SD card's mount follows. */

/* How much of one container is held at once.
 *
 * A container is fetched a page at a time and kept until this many rows are in
 * hand; anything past it is not shown. The library on this LAN has containers
 * far larger than this - "All Artists" is 57 and "By Album" is 769 - and that
 * is the point: a tree is meant to be walked, and nobody reads the four
 * hundredth album on an encoder. Holding all of one would also cost eight
 * round trips before the screen could be drawn.
 *
 * At about 840 bytes a row this is some 54 KB of PSRAM. */
#define DLNA_SOURCE_ENTRY_MAX 64U

/* Searches for servers and opens the first one at its root. Blocking: a search
 * listens for a couple of seconds and a listing is an HTTP round trip, so this
 * belongs on the player task, which already blocks to open a station. */
esp_err_t dlna_source_open(void);

/* Releases everything. Safe when nothing was open. */
void dlna_source_close(void);

/* True while a server is open and its listing can be read. */
bool dlna_source_is_open(void);

/* True from the moment the source is asked for until the search has finished
 * and the root has been read.
 *
 * The two are different answers and the difference is visible to the user: not
 * open *yet* is a second of waiting, not open *at all* is a network with no
 * server on it. Told apart, a browser can show a busy line for the first and
 * "nothing found" for the second; run together, as they were, the wait was
 * reported as a failure - and the failure arrived while everything was in fact
 * about to work. */
bool dlna_source_is_searching(void);

size_t dlna_source_server_count(void);
/* The name to show for one of the servers found. False for an index past the
 * end. */
bool dlna_source_server_name(size_t index, char *out, size_t out_size);
/* Which one is open, and switching to another - which re-opens at its root,
 * because a place in one server's tree means nothing in another's. */
size_t dlna_source_selected_server(void);
esp_err_t dlna_source_select_server(size_t index);

/* The listing on screen: how many rows, what each one is, and what to write
 * above them. The heading is the container's own title, or the server's name
 * at the root, where the container has none. */
size_t dlna_source_entry_count(void);
bool dlna_source_entry_at(size_t index, dlna_entry_t *out);
const char *dlna_source_heading(void);
/* True when there is nowhere above this: the browser closes rather than going
 * up. */
bool dlna_source_at_root(void);

/* Opens the container on `index`, or goes back up one level. Both re-read the
 * listing, so both block. */
esp_err_t dlna_source_enter(size_t index);
esp_err_t dlna_source_leave(void);
/* Reads the current container again, for a listing that failed or has gone
 * stale. */
esp_err_t dlna_source_refresh(void);

/* What choosing a row did.
 *
 * A listing holds both containers and tracks, so one gesture means two things,
 * and the row decides which. Kept here rather than at the caller because the
 * browser screen and the web interface both need the same answer, and they
 * used to work it out separately for the file browser until the two drifted. */
typedef enum {
    /* Not something that can be opened or played: a video, a codec this build
     * has no decoder for, an item the server described without a resource. */
    DLNA_ACTIVATE_REFUSED = 0,
    /* A container was opened: the listing has been replaced. */
    DLNA_ACTIVATE_BROWSED,
    /* A track started, and the tracks after it in this listing follow. */
    DLNA_ACTIVATE_PLAYING,
    /* A container that was asked for and would not open. Its own answer,
     * because it is the one outcome the user has to be told about: the rows do
     * not change, so without a word the press looks like it was dropped -
     * which on a server here means ten seconds of a screen doing nothing. */
    DLNA_ACTIVATE_BROWSE_FAILED,
} dlna_activate_t;

/* Opens the row on `index` if it is a container, plays it if it is a track.
 * Blocks either way - both are an HTTP round trip. */
dlna_activate_t dlna_source_activate(size_t index);

/* Plays the track on `index`, and goes on into the tracks after it in the same
 * listing. False when that row is not something this device can play. */
bool dlna_source_play(size_t index);

/* What "play" means on a stopped media server: the row it was last on, or -
 * when there is none, which is every browser that has not played anything yet -
 * the first playable row of the open container. False only when the container
 * holds nothing this device can play. */
bool dlna_source_start_saved(void);
/* Which row is playing, or DLNA_SOURCE_ENTRY_MAX when none is. */
size_t dlna_source_playing_index(void);
/* Moves to the next or previous playable row without waiting for the current
 * track to end. */
bool dlna_source_skip(bool forward);

#ifdef __cplusplus
}
#endif
