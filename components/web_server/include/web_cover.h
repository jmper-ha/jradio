#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The published cover, as bytes a browser will draw.
 *
 * BMP rather than PNG or JPEG because nothing on the device can encode either:
 * the cover is held as decoded RGB565 pixels - the original bytes are long
 * gone by the time a track is playing - so anything compressed would mean an
 * encoder in firmware for a picture that travels over a LAN. At 96 pixels
 * square the uncompressed answer is 27 KB, which is cheaper than the code an
 * encoder would cost.
 *
 * The awkward parts of the format are all here, where the host tests can reach
 * them: rows run bottom-up, each is padded to a multiple of four bytes, and
 * the channel order is BGR rather than RGB. */

#define WEB_COVER_BMP_HEADER_SIZE 54U

/* Bytes one row occupies in the file, padding included. */
size_t web_cover_bmp_stride(uint16_t width);
size_t web_cover_bmp_size(uint16_t width, uint16_t height);

/* Writes the 54-byte file and info headers. False when the buffer is too
 * small or the picture has no area, so a caller cannot start a response it
 * has no body for. */
bool web_cover_bmp_header(uint8_t *output, size_t output_size, uint16_t width,
                          uint16_t height);

/* One row of RGB565 as the file wants it, padding bytes included and zeroed.
 * The caller walks the picture from its last row to its first; the format's
 * bottom-up order is not applied here, because a caller streaming the file
 * has to hold that order anyway to know which row comes next.
 *
 * The five and six bit channels are expanded by repeating their top bits, not
 * by shifting in zeroes: 0x1F has to come back as 0xFF, or white turns grey. */
bool web_cover_bmp_row(uint8_t *output, size_t output_size,
                       const uint16_t *pixels, uint16_t width);
