#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The second picture decoder, for what the first one cannot read.
 *
 * tjpgd decodes a baseline JPEG in four kilobytes of working memory, and that
 * is why it is the one on the normal path - but it is baseline only, and it
 * knows nothing about PNG. Both gaps show up as a cover the device has but
 * will not display: jd_prepare() answers a progressive JPEG with JDR_FMT3, and
 * a folder cover saved as Cover.png never reaches it at all.
 *
 * This one is stb_image, which reads both by holding the whole picture at
 * once. That is the trade: roughly 13 bytes of PSRAM per source pixel while it
 * runs, against tjpgd's fixed four kilobytes, which is affordable only because
 * it happens once per track.
 */

/* Whether a JPEG is progressive, read from its start-of-frame marker.
 *
 * Cheap and exact, and it keeps the fallback honest: a JPEG this returns false
 * for is one tjpgd should have decoded, so a failure there is a broken picture
 * rather than an unsupported one, and re-reading it with a decoder that needs
 * megabytes would waste them to arrive at the same answer. */
bool image_decode_is_progressive_jpeg(const uint8_t *data, size_t length);

// True for the PNG signature. PNG has no other decoder here, so unlike a
// progressive JPEG it goes straight to this one.
bool image_decode_is_png(const uint8_t *data, size_t length);

/* The most pixels this will attempt, whatever the memory situation says.
 *
 * A folder cover is a full-size scan rather than the thumbnail a tag carries -
 * the one this was built for is 1076x978 - so the limit has to be well past
 * what an embedded cover ever is. Past this the picture is a booklet page, and
 * reducing one to a 96-pixel tile is not worth the wait. */
#define IMAGE_DECODE_PIXELS_MAX (1400U * 1400U)

/* Roughly what decoding costs while it runs, which is the number the device
 * checks its free PSRAM against. Exposed because it is the arithmetic behind
 * the refusals, and a constant nobody can check is a constant that rots. */
size_t image_decode_working_bytes(size_t pixels, bool png);

/* Decodes a JPEG or PNG into a freshly allocated RGB565 buffer, in the same
 * byte order tjpgd produces, so both decoders feed the same scaling path.
 *
 * The caller owns the buffer and releases it with image_decode_free().
 * Returns false and leaves *pixels NULL for a picture that is too large, that
 * cannot be decoded, or that there is no memory for.
 */
bool image_decode_rgb565(const uint8_t *data, size_t length, uint16_t **pixels,
                         uint16_t *width, uint16_t *height);

void image_decode_free(uint16_t *pixels);

#ifdef __cplusplus
}
#endif
