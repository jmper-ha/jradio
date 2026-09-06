#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "dlna_didl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Where in a server's tree the browser is, and how it gets back out.
 *
 * A media server's tree is addressed by opaque object ids, not by a path: the
 * id of a container says nothing about what contains it, and a listing does
 * not name its parent in a form that can be followed. So going up cannot be
 * derived the way a file browser derives it by cutting a path at the last
 * slash - the trail has to be remembered on the way in.
 *
 * That trail is also what the screen needs: after leaving a container, the
 * heading has to show the one it landed in, and only this remembers its name.
 *
 * A DIDL item carries a `parentID`, which looks like it would do instead. It
 * would get one level right and no more, and it would still leave nothing to
 * put in the heading. */

/* How deep the browser will go. The tree measured on the server here is five
 * levels from the root to a track - root, Music, library, "By Album", album -
 * and folder views nest further. Twelve is room for that with margin, at
 * 192 bytes a level. Reaching it is not a failure worth a message: it is a
 * server nesting deeper than any music collection needs, and the browser
 * simply will not open the next one. */
#define DLNA_BROWSE_DEPTH_MAX 12U

typedef struct {
    char id[DLNA_OBJECT_ID_MAX];
    char title[DLNA_TITLE_MAX];
} dlna_browse_level_t;

typedef struct {
    dlna_browse_level_t levels[DLNA_BROWSE_DEPTH_MAX];
    /* Never zero once reset: the root is a level like any other, so that
     * "where am I" always has an answer and nothing has to special-case it. */
    size_t depth;
} dlna_browse_stack_t;

/* Puts the browser at the root of `server_name`'s tree. The name is what the
 * heading shows at the top level, where the container has no title of its own -
 * "0" is an id, not something to put on a screen. */
void dlna_browse_stack_reset(dlna_browse_stack_t *stack, const char *server_name);

/* Opens a container. False when the tree is deeper than the browser goes, or
 * the entry is not something that can be opened, leaving the browser exactly
 * where it was - a half-entered level would show one container's listing under
 * another's name. */
bool dlna_browse_stack_enter(dlna_browse_stack_t *stack, const dlna_entry_t *entry);

/* Goes back up one level. False at the root, which is what tells the caller to
 * close the browser rather than to browse again. */
bool dlna_browse_stack_leave(dlna_browse_stack_t *stack);

/* What to browse, and what to write above it. Never NULL, so a caller has
 * nothing to check before issuing a request. */
const char *dlna_browse_stack_id(const dlna_browse_stack_t *stack);
const char *dlna_browse_stack_title(const dlna_browse_stack_t *stack);
bool dlna_browse_stack_at_root(const dlna_browse_stack_t *stack);

#ifdef __cplusplus
}
#endif
