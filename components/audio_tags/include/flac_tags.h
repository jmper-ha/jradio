#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "audio_tags.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FLAC_SIGNATURE_SIZE 4U
#define FLAC_BLOCK_HEADER_SIZE 4U
#define FLAC_BLOCK_VORBIS_COMMENT 4U
#define FLAC_BLOCK_PICTURE 6U
// The one picture worth showing; a file can also carry a back cover, a band
// photo and a 32x32 file icon.
#define FLAC_PICTURE_TYPE_FRONT_COVER 3U

bool flac_signature_matches(const uint8_t *bytes, size_t length);

/* Splits the four bytes that introduce every metadata block. `last` marks the
 * block after which the audio frames begin. */
bool flac_block_header_parse(const uint8_t *header, size_t length, bool *last, uint8_t *type,
                             size_t *block_length);

/* Reads TITLE / ARTIST / ALBUM out of a VORBIS_COMMENT block. Vorbis comments
 * are UTF-8 by definition, so there is no encoding byte to interpret here. */
bool flac_vorbis_comment_parse(const uint8_t *block, size_t length, audio_tags_t *tags);

/* Describes a PICTURE block without needing the image itself: the header runs
 * to a few dozen bytes while the image runs to hundreds of kilobytes, so the
 * caller reads only the front of the block and seeks to the rest. */
bool flac_picture_describe(const uint8_t *block, size_t length,
                           audio_tags_picture_format_t *format, uint8_t *picture_type,
                           size_t *data_offset, size_t *data_length);

#ifdef __cplusplus
}
#endif
