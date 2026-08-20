#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "audio_tags.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Everything the reader needs to hold at once: the largest tag it will read in
 * full. Tags run to tens of kilobytes because of the cover - the one in
 * "Вместе с нами по морям" is 68 KB - while the text in front of it is under
 * two. A tag bigger than this still yields its text, because the frames that
 * carry it come first; only the cover is given up. */
#define AUDIO_TAGS_SCRATCH_SIZE (128U * 1024U)

/* Reads the tags of an open file. Uses `scratch` as its only working memory,
 * and leaves the picture there rather than copying it: `tags->picture_offset`
 * is an index into `scratch`.
 *
 * The file position is left wherever the read ended - the caller has to seek
 * back before handing the file to a decoder. */
bool audio_tags_read_file(FILE *file, uint8_t *scratch, size_t capacity, audio_tags_t *tags);

#ifdef __cplusplus
}
#endif
