#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
} image_rect_t;

/* The largest rectangle with the source's proportions that fits inside a
 * square of `box` pixels, centred in it. Covers are square often enough that
 * this usually returns the whole box - and wrong often enough that stretching
 * them to fit would show. */
image_rect_t image_fit_square(uint16_t source_width, uint16_t source_height, uint16_t box);

void image_fill_rgb565(uint16_t *pixels, size_t count, uint16_t colour);

/* Resamples an RGB565 image into a rectangle of `destination`, averaging every
 * source pixel that falls inside each destination one.
 *
 * Averaging rather than picking: a 250-pixel cover reduced to 96 by nearest
 * neighbour drops two of every three pixels, and on lettering - which is what
 * a cover mostly is at this size - that reads as noise. */
bool image_scale_rgb565(const uint16_t *source, uint16_t source_width, uint16_t source_height,
                        uint16_t *destination, uint16_t destination_width,
                        uint16_t destination_height, uint16_t destination_stride);

#ifdef __cplusplus
}
#endif
