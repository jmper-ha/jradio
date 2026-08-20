#include "image_scale.h"

image_rect_t image_fit_square(uint16_t source_width, uint16_t source_height, uint16_t box)
{
    image_rect_t rect = {0U, 0U, 0U, 0U};
    if (source_width == 0U || source_height == 0U || box == 0U) return rect;

    uint32_t width = box;
    uint32_t height = box;
    if (source_width >= source_height) {
        height = ((uint32_t)box * source_height + source_width / 2U) / source_width;
    } else {
        width = ((uint32_t)box * source_width + source_height / 2U) / source_height;
    }
    // Rounding a very wide image can reach zero, and a zero-height rectangle
    // is not a picture.
    if (width == 0U) width = 1U;
    if (height == 0U) height = 1U;

    rect.width = (uint16_t)width;
    rect.height = (uint16_t)height;
    rect.x = (uint16_t)((box - width) / 2U);
    rect.y = (uint16_t)((box - height) / 2U);
    return rect;
}

void image_fill_rgb565(uint16_t *pixels, size_t count, uint16_t colour)
{
    if (pixels == NULL) return;
    for (size_t index = 0U; index < count; ++index) pixels[index] = colour;
}

bool image_scale_rgb565(const uint16_t *source, uint16_t source_width, uint16_t source_height,
                        uint16_t *destination, uint16_t destination_width,
                        uint16_t destination_height, uint16_t destination_stride)
{
    if (source == NULL || destination == NULL) return false;
    if (source_width == 0U || source_height == 0U) return false;
    if (destination_width == 0U || destination_height == 0U) return false;
    if (destination_stride < destination_width) return false;

    for (uint16_t y = 0U; y < destination_height; ++y) {
        // The span is computed from the edges rather than from a step, so the
        // rounding error cannot accumulate down the image.
        uint32_t y_start = ((uint32_t)y * source_height) / destination_height;
        uint32_t y_end = ((uint32_t)(y + 1U) * source_height) / destination_height;
        if (y_end <= y_start) y_end = y_start + 1U;
        if (y_end > source_height) y_end = source_height;

        for (uint16_t x = 0U; x < destination_width; ++x) {
            uint32_t x_start = ((uint32_t)x * source_width) / destination_width;
            uint32_t x_end = ((uint32_t)(x + 1U) * source_width) / destination_width;
            if (x_end <= x_start) x_end = x_start + 1U;
            if (x_end > source_width) x_end = source_width;

            uint32_t red = 0U;
            uint32_t green = 0U;
            uint32_t blue = 0U;
            uint32_t count = 0U;
            for (uint32_t sy = y_start; sy < y_end; ++sy) {
                const uint16_t *row = source + sy * source_width;
                for (uint32_t sx = x_start; sx < x_end; ++sx) {
                    const uint16_t pixel = row[sx];
                    red += (pixel >> 11) & 0x1FU;
                    green += (pixel >> 5) & 0x3FU;
                    blue += pixel & 0x1FU;
                    ++count;
                }
            }
            if (count == 0U) count = 1U;
            const uint16_t packed = (uint16_t)((((red + count / 2U) / count) << 11) |
                                               (((green + count / 2U) / count) << 5) |
                                               ((blue + count / 2U) / count));
            destination[(size_t)y * destination_stride + x] = packed;
        }
    }
    return true;
}
