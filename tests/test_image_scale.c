#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "image_scale.h"

#define RGB565(r, g, b) ((uint16_t)(((r) << 11) | ((g) << 5) | (b)))

static void test_a_square_cover_fills_the_whole_tile(void)
{
    // The common case: covers are square, and the tile is too.
    const image_rect_t rect = image_fit_square(250U, 250U, 96U);
    assert(rect.x == 0U && rect.y == 0U);
    assert(rect.width == 96U && rect.height == 96U);
}

static void test_a_wide_cover_is_letterboxed_rather_than_stretched(void)
{
    const image_rect_t rect = image_fit_square(600U, 300U, 96U);
    assert(rect.width == 96U);
    assert(rect.height == 48U);
    assert(rect.x == 0U);
    assert(rect.y == 24U);
}

static void test_a_tall_cover_is_pillarboxed(void)
{
    const image_rect_t rect = image_fit_square(300U, 600U, 96U);
    assert(rect.width == 48U);
    assert(rect.height == 96U);
    assert(rect.x == 24U);
    assert(rect.y == 0U);
}

static void test_an_extreme_ratio_still_has_a_pixel(void)
{
    // A banner 1000 wide and 3 tall rounds to zero height, and a rectangle of
    // no height is not something that can be drawn.
    const image_rect_t rect = image_fit_square(1000U, 3U, 96U);
    assert(rect.height >= 1U);
    assert(rect.width == 96U);
}

static void test_nothing_to_fit_is_an_empty_rectangle(void)
{
    const image_rect_t rect = image_fit_square(0U, 250U, 96U);
    assert(rect.width == 0U && rect.height == 0U);
}

static void test_halving_averages_every_block_of_four(void)
{
    /* Two by two of one colour each, reduced to one pixel: the answer is the
     * mean, not whichever pixel happened to be sampled. */
    const uint16_t source[4] = {RGB565(31U, 0U, 0U), RGB565(0U, 0U, 0U), RGB565(31U, 0U, 0U),
                                RGB565(0U, 0U, 0U)};
    uint16_t destination = 0U;
    assert(image_scale_rgb565(source, 2U, 2U, &destination, 1U, 1U, 1U));
    assert(((destination >> 11) & 0x1FU) == 16U);
    assert(((destination >> 5) & 0x3FU) == 0U);
    assert((destination & 0x1FU) == 0U);
}

static void test_a_flat_image_survives_any_reduction(void)
{
    /* Averaging must not shift a colour that never varies - a drift here would
     * tint every cover. */
    uint16_t source[250U * 250U];
    const uint16_t colour = RGB565(9U, 40U, 21U);
    for (size_t index = 0U; index < 250U * 250U; ++index) source[index] = colour;

    uint16_t destination[96U * 96U];
    assert(image_scale_rgb565(source, 250U, 250U, destination, 96U, 96U, 96U));
    for (size_t index = 0U; index < 96U * 96U; ++index) assert(destination[index] == colour);
}

static void test_writing_into_a_letterbox_leaves_the_margins_alone(void)
{
    /* The scaled image lands in a rectangle inside the tile, so it is written
     * with the tile's stride rather than its own width. Getting that wrong
     * shears the picture across the square. */
    uint16_t tile[8U * 8U];
    const uint16_t background = RGB565(1U, 2U, 3U);
    image_fill_rgb565(tile, 8U * 8U, background);

    uint16_t source[4U * 4U];
    const uint16_t colour = RGB565(31U, 63U, 31U);
    for (size_t index = 0U; index < 4U * 4U; ++index) source[index] = colour;

    const image_rect_t rect = image_fit_square(4U, 2U, 8U);
    assert(image_scale_rgb565(source, 4U, 2U, tile + (size_t)rect.y * 8U + rect.x, rect.width,
                              rect.height, 8U));
    for (uint16_t y = 0U; y < 8U; ++y) {
        for (uint16_t x = 0U; x < 8U; ++x) {
            const bool inside = y >= rect.y && y < rect.y + rect.height && x >= rect.x &&
                                x < rect.x + rect.width;
            assert(tile[(size_t)y * 8U + x] == (inside ? colour : background));
        }
    }
}

static void test_growing_a_small_cover_repeats_pixels(void)
{
    // A cover smaller than the tile is stretched rather than left as a stamp
    // in the middle of an empty square.
    const uint16_t source[2] = {RGB565(31U, 0U, 0U), RGB565(0U, 0U, 31U)};
    uint16_t destination[4];
    assert(image_scale_rgb565(source, 2U, 1U, destination, 4U, 1U, 4U));
    assert(destination[0] == source[0]);
    assert(destination[1] == source[0]);
    assert(destination[2] == source[1]);
    assert(destination[3] == source[1]);
}

static void test_impossible_arguments_are_refused(void)
{
    uint16_t pixel = 0U;
    assert(!image_scale_rgb565(NULL, 2U, 2U, &pixel, 1U, 1U, 1U));
    assert(!image_scale_rgb565(&pixel, 0U, 2U, &pixel, 1U, 1U, 1U));
    assert(!image_scale_rgb565(&pixel, 2U, 2U, &pixel, 1U, 1U, 0U));
}

int main(void)
{
    test_a_square_cover_fills_the_whole_tile();
    test_a_wide_cover_is_letterboxed_rather_than_stretched();
    test_a_tall_cover_is_pillarboxed();
    test_an_extreme_ratio_still_has_a_pixel();
    test_nothing_to_fit_is_an_empty_rectangle();
    test_halving_averages_every_block_of_four();
    test_a_flat_image_survives_any_reduction();
    test_writing_into_a_letterbox_leaves_the_margins_alone();
    test_growing_a_small_cover_repeats_pixels();
    test_impossible_arguments_are_refused();
    printf("image_scale tests passed\n");
    return 0;
}
