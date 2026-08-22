#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Sized by the line that motivated the feature: the album of "Вместе с нами по
 * морям" is 57 Cyrillic characters, which is 110 bytes of UTF-8. A tag cut
 * mid-word reads as a fault in the player rather than a long title. */
#define AUDIO_TAGS_TEXT_MAX_LEN 128

typedef enum {
    AUDIO_TAGS_PICTURE_NONE = 0,
    AUDIO_TAGS_PICTURE_JPEG,
    AUDIO_TAGS_PICTURE_PNG,
    // A picture that is there but in a format nothing here can draw. Kept
    // apart from NONE so a caller can say "there is a cover, I cannot show it"
    // instead of "this file has no cover".
    AUDIO_TAGS_PICTURE_UNSUPPORTED,
} audio_tags_picture_format_t;

typedef struct {
    char title[AUDIO_TAGS_TEXT_MAX_LEN];
    char artist[AUDIO_TAGS_TEXT_MAX_LEN];
    char album[AUDIO_TAGS_TEXT_MAX_LEN];
    audio_tags_picture_format_t picture_format;
    /* Where the picture sits inside the buffer that was parsed. The parsers
     * never copy it: a cover is two orders of magnitude larger than the text
     * around it, and the caller already holds those bytes.
     *
     * Zero length means the picture is not in that buffer - either there is
     * none, or it did not fit, which the two fields below tell apart. */
    size_t picture_offset;
    size_t picture_length;
    /* Where the same picture is in the file, for a caller with somewhere else
     * to put it.
     *
     * A cover of 140 KB in a file whose reader was given 128 KB used to be
     * given up on, and a whole album then played with the placeholder over a
     * picture the file was carrying. Nothing has to be read to fill these in -
     * the block walk already knows where the picture lies - so the caller can
     * read exactly it and nothing else.
     *
     * Filled by the FLAC reader. ID3v2 keeps its picture inside the body the
     * caller's buffer holds, and a tag larger than that buffer is truncated
     * before the frame is ever seen, so there is nothing to point at. */
    size_t picture_file_offset;
    size_t picture_file_length;
} audio_tags_t;

void audio_tags_clear(audio_tags_t *tags);
bool audio_tags_have_text(const audio_tags_t *tags);

// The four text encodings ID3v2 defines. Vorbis comments are always UTF-8.
#define AUDIO_TAGS_ENCODING_ISO8859_1 0U
#define AUDIO_TAGS_ENCODING_UTF16_BOM 1U
#define AUDIO_TAGS_ENCODING_UTF16_BE 2U
#define AUDIO_TAGS_ENCODING_UTF8 3U

/* Converts one tag field to UTF-8 and returns how many bytes it wrote, not
 * counting the terminator. Always terminates when `capacity` is non-zero.
 *
 * Truncation stops on a character boundary: a half-written UTF-8 sequence is
 * not a shorter string, it is a broken one, and LVGL draws the remains as a
 * stray glyph. */
size_t audio_tags_text_to_utf8(uint8_t encoding, const uint8_t *input, size_t length,
                               char *output, size_t capacity);

/* True when `input` is Latin-1 in name only and Windows-1251 in fact.
 *
 * Exposed for the tests rather than for callers: it is the one guess in the
 * whole parser, and it needs its boundaries pinned down. */
bool audio_tags_looks_like_cp1251(const uint8_t *input, size_t length);

#ifdef __cplusplus
}
#endif
