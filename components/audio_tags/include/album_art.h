#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The largest square this module will ever publish, and so the size of the one
 * buffer it holds - fixed however large the pictures in the files are.
 *
 * It is a ceiling and not the answer: how much of the screen the cover is
 * given belongs to the panel's layout, and a 480x320 panel can afford a
 * bigger tile than the 320x240 one this started on. The layout says which
 * square it wants through album_art_set_square(); this is only what the
 * allocation has to cover so that saying it costs nothing. */
#define ALBUM_ART_MAX_SIZE 160U
#define ALBUM_ART_PIXELS (ALBUM_ART_MAX_SIZE * ALBUM_ART_MAX_SIZE)

typedef struct {
    /* Changes whenever the published cover does. A screen that has drawn
     * generation N needs to do nothing at all until it sees N+1, which is what
     * keeps this off the ten-millisecond poll loop. */
    unsigned int generation;
    /* A checksum of the bytes the cover was decoded from - the same picture
     * has the same signature across reboots, and two different pictures
     * practically never share one. The generation says *that* the cover
     * changed; this says *which* cover it is, which is what a cache key has to
     * be. Zero when nothing is published. */
    uint32_t signature;
    bool present;
    // The fitted size, which is the square only for a square picture. The
    // caller centres it; nothing here paints a border or a background.
    uint16_t width;
    uint16_t height;
} album_art_status_t;

esp_err_t album_art_init(void);

/* The square pictures are fitted into from here on, in pixels, clamped to
 * ALBUM_ART_MAX_SIZE. Called by the UI once at start-up with the tile its
 * layout keeps for the cover.
 *
 * Settable rather than compiled in because the two ends cannot see each other:
 * the size belongs to ui_layout.h, and this component is a leaf that nothing
 * else depends on - including the layout here would mean depending on the
 * display, which is a strange thing for a tag decoder to do. Order does not
 * matter; it is read when a cover is decoded, which is long after boot. */
void album_art_set_square(uint16_t square);

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
