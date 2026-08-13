#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "usb_browser.h"

/* Why the USB source needs its own notice text at all: selecting it is the one
 * menu entry that can fail for a reason the user can fix, and the screen
 * switches optimistically before the controller accepts the command. Without a
 * message the user just lands on an empty browser and has no idea whether the
 * drive is missing, unreadable, or simply empty.
 *
 * The three media states are deliberately not collapsed into a bool: telling
 * someone to insert a drive they already inserted is worse than saying nothing,
 * because it sends them looking in the wrong place. */

// Returns the notice to show instead of entering the browser, or NULL when the
// source can be opened. `entry_count` distinguishes a readable but empty
// directory, which is not an error and gets its own wording.
const char *ui_usb_notice(usb_browser_media_t media, size_t entry_count);

// True when the source may be opened, i.e. exactly when ui_usb_notice returns
// NULL. Kept as its own predicate so callers read as intent rather than as a
// NULL check.
bool ui_usb_can_open(usb_browser_media_t media, size_t entry_count);
