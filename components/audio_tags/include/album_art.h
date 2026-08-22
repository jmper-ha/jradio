#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The side of the square the player screen keeps for the cover. Everything is
 * reduced to this before it is published, so the memory this module holds is
 * fixed however large the pictures in the files are. */
#define ALBUM_ART_SIZE 96U
#define ALBUM_ART_PIXELS (ALBUM_ART_SIZE * ALBUM_ART_SIZE)

typedef struct {
    /* Changes whenever the published cover does. A screen that has drawn
     * generation N needs to do nothing at all until it sees N+1, which is what
     * keeps this off the ten-millisecond poll loop. */
    unsigned int generation;
    bool present;
    // The fitted size, which is the square only for a square picture. The
    // caller centres it; nothing here paints a border or a background.
    uint16_t width;
    uint16_t height;
} album_art_status_t;

esp_err_t album_art_init(void);

/* Decodes a picture and publishes it as the current cover. JPEG, baseline or
 * progressive, and PNG - the caller hands over bytes and does not care which,
 * because a cover out of a tag and a cover out of a file beside the track are
 * the same thing by the time they reach here.
 *
 * Decoding the same bytes twice is free: every track of an album carries the
 * same picture - and a folder cover is literally the same file - so re-reading
 * it would blank the tile for a moment on each track change.
 *
 * Runs on the caller's task and takes tens of milliseconds, so it belongs
 * where a track is being opened, not where one is being drawn. */
bool album_art_set_image(const uint8_t *data, size_t length);

void album_art_clear(void);
album_art_status_t album_art_status(void);

/* Copies the published cover out. The caller ends up owning pixels nothing
 * else writes to, which is what lets LVGL hold on to them for as long as the
 * image object lives. */
bool album_art_copy(uint16_t *destination, size_t capacity_pixels, uint16_t *width,
                    uint16_t *height);

#ifdef __cplusplus
}
#endif
