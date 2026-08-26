#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "web_cover.h"

static uint32_t read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint16_t read_u16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

/* Rows are padded to four bytes, which for a width that is not a multiple of
 * four means the file is larger than the pixels in it. Ninety-six - the size
 * the device actually publishes - happens to need no padding, so both cases
 * are checked or the padding would only be exercised by accident. */
static void test_stride_rounds_up_to_four_bytes(void)
{
    assert(web_cover_bmp_stride(96U) == 288U);
    assert(web_cover_bmp_stride(1U) == 4U);
    assert(web_cover_bmp_stride(2U) == 8U);
    assert(web_cover_bmp_stride(3U) == 12U);
    assert(web_cover_bmp_stride(5U) == 16U);

    assert(web_cover_bmp_size(96U, 96U) == 54U + 288U * 96U);
    assert(web_cover_bmp_size(3U, 2U) == 54U + 12U * 2U);
    // Nothing to serve, and a header claiming otherwise would be worse.
    assert(web_cover_bmp_size(0U, 10U) == 0U);
    assert(web_cover_bmp_size(10U, 0U) == 0U);
}

static void test_header_describes_the_picture(void)
{
    uint8_t header[WEB_COVER_BMP_HEADER_SIZE];
    assert(web_cover_bmp_header(header, sizeof(header), 3U, 2U));
    assert(header[0] == 'B' && header[1] == 'M');
    assert(read_u32(header + 2) == 54U + 12U * 2U);
    assert(read_u32(header + 10) == WEB_COVER_BMP_HEADER_SIZE);
    assert(read_u32(header + 14) == 40U);
    assert(read_u32(header + 18) == 3U);
    /* Positive height, which is what says the rows run bottom-up. A negative
     * one would mean top-down, and not every decoder honours it. */
    assert(read_u32(header + 22) == 2U);
    assert(read_u16(header + 26) == 1U);
    assert(read_u16(header + 28) == 24U);
    // Uncompressed; the field is left at zero for BI_RGB.
    assert(read_u32(header + 30) == 0U);
    assert(read_u32(header + 34) == 12U * 2U);

    uint8_t small[WEB_COVER_BMP_HEADER_SIZE - 1U];
    assert(!web_cover_bmp_header(small, sizeof(small), 3U, 2U));
    assert(!web_cover_bmp_header(header, sizeof(header), 0U, 2U));
    assert(!web_cover_bmp_header(NULL, sizeof(header), 3U, 2U));
}

/* The five and six bit channels are widened by repeating their top bits. The
 * point of the exercise is the extremes: 0x1F has to come back as 0xFF, or a
 * white cover arrives grey. */
static void test_row_expands_the_channels_and_pads(void)
{
    const uint16_t pixels[3] = {
        0xFFFFU,  // white
        0x0000U,  // black
        0xF800U,  // pure red
    };
    uint8_t row[16];
    memset(row, 0xAAU, sizeof(row));
    assert(web_cover_bmp_row(row, sizeof(row), pixels, 3U));

    // BGR, not RGB.
    assert(row[0] == 0xFFU && row[1] == 0xFFU && row[2] == 0xFFU);
    assert(row[3] == 0x00U && row[4] == 0x00U && row[5] == 0x00U);
    assert(row[6] == 0x00U && row[7] == 0x00U && row[8] == 0xFFU);
    // The three padding bytes are written, not left as whatever was there.
    assert(row[9] == 0x00U && row[10] == 0x00U && row[11] == 0x00U);
    assert(row[12] == 0xAAU);

    // Green is the six-bit channel, and widens by two bits rather than three.
    const uint16_t green = 0x07E0U;
    assert(web_cover_bmp_row(row, sizeof(row), &green, 1U));
    assert(row[0] == 0x00U && row[1] == 0xFFU && row[2] == 0x00U);

    uint8_t tight[11];
    assert(!web_cover_bmp_row(tight, sizeof(tight), pixels, 3U));
    assert(!web_cover_bmp_row(row, sizeof(row), NULL, 3U));
    assert(!web_cover_bmp_row(row, sizeof(row), pixels, 0U));
}

int main(void)
{
    test_stride_rounds_up_to_four_bytes();
    test_header_describes_the_picture();
    test_row_expands_the_channels_and_pads();
    printf("web cover tests passed\n");
    return 0;
}
