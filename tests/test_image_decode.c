#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "fixtures/image_sample.h"
#include "image_decode.h"

// The fixture's quadrants in RGB565, which is what the decoder hands back.
#define RED 0xF800U
#define GREEN 0x07E0U
#define BLUE 0x001FU
#define WHITE 0xFFFFU

/* JPEG is lossy and the quadrant edges ring, so the tests read the middle of
 * each quadrant and compare channel by channel with room to spare. Anything
 * looser would pass on a decoder that swapped red and blue. */
static void assert_near(uint16_t actual, uint16_t expected)
{
    const int red = (int)((actual >> 11) & 0x1F) - (int)((expected >> 11) & 0x1F);
    const int green = (int)((actual >> 5) & 0x3F) - (int)((expected >> 5) & 0x3F);
    const int blue = (int)(actual & 0x1F) - (int)(expected & 0x1F);
    assert(abs(red) <= 3 && abs(green) <= 6 && abs(blue) <= 3);
}

static void assert_quadrants(const uint16_t *pixels)
{
    assert_near(pixels[8U * 32U + 8U], RED);
    assert_near(pixels[8U * 32U + 24U], GREEN);
    assert_near(pixels[24U * 32U + 8U], BLUE);
    assert_near(pixels[24U * 32U + 24U], WHITE);
}

static void test_a_progressive_picture_is_recognised(void)
{
    assert(image_decode_is_progressive_jpeg(image_sample_jpeg_progressive,
                                            IMAGE_SAMPLE_JPEG_PROGRESSIVE_LENGTH));
}

static void test_a_baseline_picture_is_left_to_the_other_decoder(void)
{
    /* The load-bearing half. tjpgd decodes baseline in four kilobytes; sending
     * one here anyway would spend megabytes to reach the same picture. */
    assert(!image_decode_is_progressive_jpeg(image_sample_jpeg_baseline,
                                             IMAGE_SAMPLE_JPEG_BASELINE_LENGTH));
}

static void test_rubbish_is_not_called_progressive(void)
{
    const uint8_t not_a_jpeg[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    assert(!image_decode_is_progressive_jpeg(not_a_jpeg, sizeof(not_a_jpeg)));
    assert(!image_decode_is_progressive_jpeg(NULL, 16U));
    // Truncated inside the marker walk: no frame header, so no answer.
    assert(!image_decode_is_progressive_jpeg(image_sample_jpeg_progressive, 4U));
}

static void test_png_is_told_from_jpeg(void)
{
    assert(image_decode_is_png(image_sample_png, IMAGE_SAMPLE_PNG_LENGTH));
    assert(!image_decode_is_png(image_sample_jpeg_baseline, IMAGE_SAMPLE_JPEG_BASELINE_LENGTH));
    // The signature is eight bytes; anything shorter cannot be one.
    assert(!image_decode_is_png(image_sample_png, 4U));
    assert(!image_decode_is_png(NULL, 64U));
}

static void test_a_progressive_cover_decodes_to_the_right_colours(void)
{
    uint16_t *pixels = NULL;
    uint16_t width = 0U;
    uint16_t height = 0U;
    assert(image_decode_rgb565(image_sample_jpeg_progressive,
                               IMAGE_SAMPLE_JPEG_PROGRESSIVE_LENGTH, &pixels, &width,
                               &height));
    assert(pixels != NULL);
    assert(width == 32U && height == 32U);
    assert_quadrants(pixels);
    image_decode_free(pixels);
}

static void test_a_png_folder_cover_decodes_to_the_right_colours(void)
{
    /* PNG is lossless, so this one is exact - and it is the case the folder
     * cover was added for: a rip with bare tags and a Cover.png beside it. */
    uint16_t *pixels = NULL;
    uint16_t width = 0U;
    uint16_t height = 0U;
    assert(image_decode_rgb565(image_sample_png, IMAGE_SAMPLE_PNG_LENGTH, &pixels, &width,
                               &height));
    assert(width == 32U && height == 32U);
    assert(pixels[8U * 32U + 8U] == RED);
    assert(pixels[8U * 32U + 24U] == GREEN);
    assert(pixels[24U * 32U + 8U] == BLUE);
    assert(pixels[24U * 32U + 24U] == WHITE);
    image_decode_free(pixels);
}

static void test_baseline_decodes_here_too(void)
{
    /* Not how the device uses this - tjpgd gets those - but the fallback must
     * not be a decoder that only handles half of JPEG, or a picture that is
     * progressive in a way the marker walk missed would fail twice. */
    uint16_t *pixels = NULL;
    uint16_t width = 0U;
    uint16_t height = 0U;
    assert(image_decode_rgb565(image_sample_jpeg_baseline, IMAGE_SAMPLE_JPEG_BASELINE_LENGTH,
                               &pixels, &width, &height));
    assert(width == 32U && height == 32U);
    assert_quadrants(pixels);
    image_decode_free(pixels);
}

static void test_the_memory_estimate_matches_what_was_measured(void)
{
    /* The numbers behind the device's refusals. Measured with an instrumented
     * allocator: a 250x250 progressive JPEG peaked at 12.75 bytes a pixel, a
     * 1076x978 PNG at 6.01 - so JPEG must be the dearer of the two, and the
     * folder cover that motivated all of this has to come in under the 8 MB
     * of PSRAM the device has free while it plays. */
    assert(image_decode_working_bytes(1000U, true) < image_decode_working_bytes(1000U, false));
    assert(image_decode_working_bytes(250U * 250U, false) >= 250U * 250U * 12U);
    assert(image_decode_working_bytes(1076U * 978U, true) < 7U * 1024U * 1024U);
    assert(image_decode_working_bytes(0U, true) == 0U);
}

static void test_a_picture_that_cannot_be_decoded_reports_failure(void)
{
    uint16_t *pixels = (uint16_t *)0x1;
    const uint8_t truncated[16] = {0xFF, 0xD8, 0xFF, 0xC2, 0x00, 0x11, 0x08};
    assert(!image_decode_rgb565(truncated, sizeof(truncated), &pixels, NULL, NULL));
    // Cleared even on failure, so a caller that frees unconditionally is safe.
    assert(pixels == NULL);
    assert(!image_decode_rgb565(NULL, 32U, &pixels, NULL, NULL));
    assert(!image_decode_rgb565(image_sample_png, 0U, &pixels, NULL, NULL));
    assert(!image_decode_rgb565(image_sample_png, IMAGE_SAMPLE_PNG_LENGTH, NULL, NULL, NULL));
    // A PNG cut in half: the header parses, the pixels do not.
    assert(!image_decode_rgb565(image_sample_png, IMAGE_SAMPLE_PNG_LENGTH / 2U, &pixels,
                                NULL, NULL));
    // Nothing to free: the failing paths never allocated.
    image_decode_free(NULL);
}

int main(void)
{
    test_a_progressive_picture_is_recognised();
    test_a_baseline_picture_is_left_to_the_other_decoder();
    test_rubbish_is_not_called_progressive();
    test_png_is_told_from_jpeg();
    test_a_progressive_cover_decodes_to_the_right_colours();
    test_a_png_folder_cover_decodes_to_the_right_colours();
    test_baseline_decodes_here_too();
    test_the_memory_estimate_matches_what_was_measured();
    test_a_picture_that_cannot_be_decoded_reports_failure();
    puts("image_decode tests passed");
    return 0;
}
