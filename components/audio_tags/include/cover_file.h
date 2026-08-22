#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Recognises the picture a folder keeps for the tracks in it.
 *
 * Most files carry their cover in their own tag, and that one always wins: it
 * belongs to the track rather than to whatever else happens to sit in the same
 * directory. But a rip can just as easily leave the tags bare and drop one
 * "Cover.png" beside the tracks, and there the screen used to show the
 * placeholder over a picture the drive was holding all along.
 *
 * Deliberately one name and three extensions. "folder.jpg", "front.jpg",
 * "albumart*.jpg" and the rest are conventions of particular music players,
 * and guessing at them means opening files that turn out not to be pictures on
 * every track of every album that has none.
 */

// True for cover.jpg, cover.jpeg and cover.png in any mixture of upper and
// lower case, and for nothing else. ASCII only: the name is an English word
// however the drive is written.
bool cover_file_name_matches(const char *name);

#ifdef __cplusplus
}
#endif
