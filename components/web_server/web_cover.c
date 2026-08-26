#include "web_cover.h"

#include <string.h>

static void write_u16(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)(value & 0xFFU);
    output[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static void write_u32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value & 0xFFU);
    output[1] = (uint8_t)((value >> 8) & 0xFFU);
    output[2] = (uint8_t)((value >> 16) & 0xFFU);
    output[3] = (uint8_t)((value >> 24) & 0xFFU);
}

size_t web_cover_bmp_stride(uint16_t width)
{
    const size_t row = (size_t)width * 3U;
    return (row + 3U) & ~(size_t)3U;
}

size_t web_cover_bmp_size(uint16_t width, uint16_t height)
{
    if (width == 0U || height == 0U) return 0U;
    return WEB_COVER_BMP_HEADER_SIZE + web_cover_bmp_stride(width) * (size_t)height;
}

bool web_cover_bmp_header(uint8_t *output, size_t output_size, uint16_t width,
                          uint16_t height)
{
    const size_t total = web_cover_bmp_size(width, height);
    if (output == NULL || total == 0U || output_size < WEB_COVER_BMP_HEADER_SIZE) {
        return false;
    }
    memset(output, 0, WEB_COVER_BMP_HEADER_SIZE);
    output[0] = 'B';
    output[1] = 'M';
    write_u32(output + 2, (uint32_t)total);
    write_u32(output + 10, WEB_COVER_BMP_HEADER_SIZE);
    write_u32(output + 14, 40U);
    write_u32(output + 18, width);
    /* Positive, which is what says the rows run bottom-up. A negative height
     * would let them run top-down, but not every decoder honours it. */
    write_u32(output + 22, height);
    write_u16(output + 26, 1U);
    write_u16(output + 28, 24U);
    write_u32(output + 34,
              (uint32_t)(web_cover_bmp_stride(width) * (size_t)height));
    return true;
}

bool web_cover_bmp_row(uint8_t *output, size_t output_size,
                       const uint16_t *pixels, uint16_t width)
{
    const size_t stride = web_cover_bmp_stride(width);
    if (output == NULL || pixels == NULL || width == 0U || output_size < stride) {
        return false;
    }
    for (uint16_t column = 0U; column < width; ++column) {
        const uint16_t pixel = pixels[column];
        const uint8_t red = (uint8_t)((pixel >> 11) & 0x1FU);
        const uint8_t green = (uint8_t)((pixel >> 5) & 0x3FU);
        const uint8_t blue = (uint8_t)(pixel & 0x1FU);
        uint8_t *target = output + (size_t)column * 3U;
        target[0] = (uint8_t)((blue << 3) | (blue >> 2));
        target[1] = (uint8_t)((green << 2) | (green >> 4));
        target[2] = (uint8_t)((red << 3) | (red >> 2));
    }
    memset(output + (size_t)width * 3U, 0, stride - (size_t)width * 3U);
    return true;
}
