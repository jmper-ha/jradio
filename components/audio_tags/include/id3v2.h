#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "audio_tags.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ID3V2_HEADER_SIZE 10U
#define ID3V1_BLOCK_SIZE 128U

typedef struct {
    uint8_t major_version;
    bool unsynchronised;
    bool has_extended_header;
    bool has_footer;
    // Bytes after the 10-byte header, as the tag declares them. The footer, if
    // any, is not part of it.
    size_t body_size;
} id3v2_header_t;

/* Reads the ten bytes that open the file. False means there is no ID3v2 tag
 * here, or one of a version this cannot walk. */
bool id3v2_header_parse(const uint8_t *header, size_t length, id3v2_header_t *out);

/* Removes the zero byte inserted after every 0xFF, in place, and returns the
 * new length.
 *
 * Only the tag-wide flag is handled here, which is the one writers actually
 * set. It has to happen before the frames are walked: with the padding still
 * in, every frame size is wrong by however many 0xFF bytes preceded it. */
size_t id3v2_deunsynchronise(uint8_t *body, size_t length);

/* Walks the frames and fills whatever it recognises, leaving the rest of
 * `tags` untouched.
 *
 * `length` may be smaller than `header->body_size`: a tag whose cover did not
 * fit the buffer still yields its text, and that is worth more than failing
 * the whole read. `tags->picture_offset` is relative to `body`. */
bool id3v2_parse_body(const id3v2_header_t *header, const uint8_t *body, size_t length,
                      audio_tags_t *tags);

/* The 128 bytes at the end of the file. Only a fallback: it holds 30
 * characters per field in an unnamed encoding, so anything ID3v2 said wins. */
bool id3v1_parse(const uint8_t *block, size_t length, audio_tags_t *tags);

#ifdef __cplusplus
}
#endif
