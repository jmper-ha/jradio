#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Whether a container at the *root* of a server's tree is worth showing to a
 * music player.
 *
 * There is nothing structural to go on, and that was measured rather than
 * assumed: the server here publishes "Video", "Music" and "Photos" as three
 * identical `object.container.storageFolder` entries, with `upnp:genre` set to
 * "Unknown" on all three and `dc:description` repeating the title. The name is
 * the only thing that differs, so the name is what this reads.
 *
 * It is therefore a deny list, not an allow list: a section is hidden only
 * when its name is one that servers actually use for video or pictures, and
 * anything unrecognised is shown. A server whose sections are named in a
 * language or a style not listed here loses nothing - it just keeps the rows
 * it always had. Callers must also keep every row when the filter would empty
 * the listing, so a mistake here can never leave a blank root.
 *
 * Only the root: below it a container's name means whatever the library's
 * owner meant, and an album called "Photos" is a perfectly ordinary album. */
bool dlna_root_filter_is_music(const char *title);

#ifdef __cplusplus
}
#endif
