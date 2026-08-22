#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "fixtures/jpeg_sample.h"
#include "jpeg_progressive.h"

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

static void test_a_progressive_picture_is_recognised(void)
{
    assert(jpeg_is_progressive(jpeg_sample_progressive, JPEG_SAMPLE_PROGRESSIVE_LENGTH));
}

static void test_a_baseline_picture_is_left_to_the_other_decoder(void)
{
    /* The load-bearing half. tjpgd decodes baseline in four kilobytes; sending
     * one here anyway would spend megabytes to reach the same picture. */
    assert(!jpeg_is_progressive(jpeg_sample_baseline, JPEG_SAMPLE_BASELINE_LENGTH));
}

static void test_rubbish_is_not_called_progressive(void)
{
    const uint8_t not_a_jpeg[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    assert(!jpeg_is_progressive(not_a_jpeg, sizeof(not_a_jpeg)));
    assert(!jpeg_is_progressive(NULL, 16U));
    // Truncated inside the marker walk: no frame header, so no answer.
    assert(!jpeg_is_progressive(jpeg_sample_progressive, 4U));
}

static void test_a_progressive_cover_decodes_to_the_right_colours(void)
{
    uint16_t *pixels = NULL;
    uint16_t width = 0U;
    uint16_t height = 0U;
    assert(jpeg_progressive_decode(jpeg_sample_progressive, JPEG_SAMPLE_PROGRESSIVE_LENGTH,
                                   &pixels, &width, &height));
    assert(pixels != NULL);
    assert(width == 32U && height == 32U);

    assert_near(pixels[8U * 32U + 8U], RED);
    assert_near(pixels[8U * 32U + 24U], GREEN);
    assert_near(pixels[24U * 32U + 8U], BLUE);
    assert_near(pixels[24U * 32U + 24U], WHITE);
    jpeg_progressive_free(pixels);
}

static void test_baseline_decodes_here_too(void)
{
    /* Not how the device uses this - tjpgd gets those - but the fallback must
     * not be a decoder that only handles half of JPEG, or a picture that is
     * progressive in a way the marker walk missed would fail twice. */
    uint16_t *pixels = NULL;
    uint16_t width = 0U;
    uint16_t height = 0U;
    assert(jpeg_progressive_decode(jpeg_sample_baseline, JPEG_SAMPLE_BASELINE_LENGTH,
                                   &pixels, &width, &height));
    assert(width == 32U && height == 32U);
    assert_near(pixels[8U * 32U + 8U], RED);
    jpeg_progressive_free(pixels);
}

static void test_a_picture_that_cannot_be_decoded_reports_failure(void)
{
    uint16_t *pixels = (uint16_t *)0x1;
    const uint8_t truncated[16] = {0xFF, 0xD8, 0xFF, 0xC2, 0x00, 0x11, 0x08};
    assert(!jpeg_progressive_decode(truncated, sizeof(truncated), &pixels, NULL, NULL));
    // Cleared even on failure, so a caller that frees unconditionally is safe.
    assert(pixels == NULL);
    assert(!jpeg_progressive_decode(NULL, 32U, &pixels, NULL, NULL));
    assert(!jpeg_progressive_decode(jpeg_sample_progressive, 0U, &pixels, NULL, NULL));
    assert(!jpeg_progressive_decode(jpeg_sample_progressive, JPEG_SAMPLE_PROGRESSIVE_LENGTH,
                                    NULL, NULL, NULL));
    // Nothing to free: the failing paths never allocated.
    jpeg_progressive_free(NULL);
}

int main(void)
{
    test_a_progressive_picture_is_recognised();
    test_a_baseline_picture_is_left_to_the_other_decoder();
    test_rubbish_is_not_called_progressive();
    test_a_progressive_cover_decodes_to_the_right_colours();
    test_baseline_decodes_here_too();
    test_a_picture_that_cannot_be_decoded_reports_failure();
    puts("jpeg_progressive tests passed");
    return 0;
}
