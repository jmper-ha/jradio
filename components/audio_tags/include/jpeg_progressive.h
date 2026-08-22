#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The second JPEG decoder, for the covers the first one refuses.
 *
 * tjpgd decodes every cover here in a few kilobytes of working memory, and
 * that is why it is the one on the normal path - but it is baseline only, and
 * jd_prepare() answers a progressive file with JDR_FMT3. Progressive is what
 * several taggers write by default, so a whole album can arrive with a cover
 * that nothing on the device can read; the screen then shows the placeholder
 * and there is nothing wrong with either the file or the drive.
 *
 * This one is stb_image, which decodes both kinds by holding the whole picture
 * at once. That is the trade: roughly 13 bytes of PSRAM per source pixel while
 * it runs, against tjpgd's fixed four kilobytes, which is affordable only
 * because it happens once per track and only when the first decoder has
 * already given up.
 */

/* Whether the picture is progressive, read from its start-of-frame marker.
 *
 * Cheap and exact, and it keeps the fallback honest: a file this returns false
 * for is one tjpgd should have decoded, so a failure there is a broken picture
 * rather than an unsupported one, and re-reading it with a decoder that needs
 * megabytes would waste them to arrive at the same answer. */
bool jpeg_is_progressive(const uint8_t *data, size_t length);

/* The most pixels this will decode, and why there is a limit at all: the
 * working set is about 13 bytes a pixel (three components held both as
 * coefficients and as samples, plus the RGB output), so 600x600 is already
 * near 5 MB. Covers larger than this exist, but reducing one to a 96-pixel
 * tile is not worth the free PSRAM of the whole device. */
#define JPEG_PROGRESSIVE_PIXELS_MAX (600U * 600U)

/* Decodes into a freshly allocated RGB565 buffer, in the same byte order
 * tjpgd produces, so both decoders feed the same scaling path.
 *
 * The caller owns the buffer and releases it with jpeg_progressive_free().
 * Returns false and leaves *pixels NULL for a picture that is too large, that
 * cannot be decoded, or that there is no memory for.
 */
bool jpeg_progressive_decode(const uint8_t *data, size_t length, uint16_t **pixels,
                             uint16_t *width, uint16_t *height);

void jpeg_progressive_free(uint16_t *pixels);

#ifdef __cplusplus
}
#endif
