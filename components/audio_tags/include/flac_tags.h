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

#define FLAC_BLOCK_STREAMINFO 0U
// Fixed by the format: sample rate, channels, depth, length and an MD5 of the
// audio, in that order.
#define FLAC_STREAMINFO_SIZE 34U

/* What STREAMINFO says about the audio.
 *
 * The reason to read it at all is `total_samples`: FLAC is variable rate, so
 * the file size and a bitrate say nothing about how long the track is, and
 * without a length the player has no progress bar to draw. The encoder wrote
 * the exact sample count here, which needs only to be divided by the rate.
 */
typedef struct {
    uint32_t sample_rate_hz;
    uint8_t channels;
    uint8_t bits_per_sample;
    // Zero when the encoder did not know - a stream piped into it, not a file.
    uint64_t total_samples;
    // Equal to each other for a fixed-block-size encode, which is what every
    // encoder in ordinary use writes. Kept because they are what tells a real
    // frame header from a byte pattern that looks like one.
    uint16_t min_block_size;
    uint16_t max_block_size;
} flac_streaminfo_t;

/* One audio frame's header, as much of it as a jump needs.
 *
 * FLAC frames are self-describing and independent of each other, which is what
 * makes a jump possible at all: land on a frame header and the decoder can
 * carry on from there without having seen a single frame before it. Landing
 * anywhere else it cannot, and says so with SYNC_NOT_FOUND. */
typedef struct {
    // Length of the header itself, which is variable: the frame number is a
    // UTF-8-style run, and block size and rate can each carry a tail.
    size_t header_bytes;
    uint32_t block_size;
    // Zero when the frame defers to STREAMINFO, which is the usual case.
    uint32_t sample_rate_hz;
    uint8_t channels;
} flac_frame_header_t;

/* Reads a frame header, and answers whether `data` really begins with one.
 *
 * The CRC-8 at its end is the point: 0xFF 0xF8 turns up inside compressed
 * audio often enough that a byte pattern alone means nothing, and a false
 * frame start feeds the decoder rubbish. */
bool flac_frame_header_parse(const uint8_t *data, size_t length, flac_frame_header_t *out);

/* Offset of the first real frame header in `data`, for finding the place a
 * jump should actually start from.
 *
 * `info` is what makes this trustworthy and may not be omitted lightly: a
 * header that passes its own CRC still turns up about once in every ten
 * megabytes of compressed audio, which over a whole file is several. Checked
 * against the stream it is supposed to belong to - rate, channels, block size
 * - those disappear. Pass NULL only where there is nothing to check against.
 */
bool flac_frame_find_sync(const uint8_t *data, size_t length,
                          const flac_streaminfo_t *info, size_t *offset);

bool flac_signature_matches(const uint8_t *bytes, size_t length);

// Reads the first metadata block of a FLAC file, which the format requires to
// be STREAMINFO. `block` is its body, without the four-byte block header.
bool flac_streaminfo_parse(const uint8_t *block, size_t length, flac_streaminfo_t *out);

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
