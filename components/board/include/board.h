#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/* The display's two flip settings are arguments, and no other setting is,
 * because this ends by drawing the boot splash: MADCTL steers where arriving
 * pixels land and moves nothing already on the glass, so a splash drawn before
 * the switches are known stays the way it was written. Everything else the
 * settings decide - brightness, volume - is applied later by the UI, which is
 * why only these two have to be here. */
esp_err_t board_init(bool flip_vertical, bool flip_horizontal);
esp_err_t board_backlight_set(uint8_t percent);
esp_err_t board_audio_write(const void *pcm, size_t pcm_length, size_t *written,
                            uint32_t timeout_ms);
esp_err_t board_audio_start(const void *pcm, size_t pcm_length, size_t *preloaded);
esp_err_t board_audio_set_sample_rate(uint32_t sample_rate);
esp_err_t board_audio_set_enabled(bool enabled);
esp_err_t board_audio_self_test(uint32_t duration_ms);
/* Monotonic count of I2S TX underruns (DMA ran dry mid-playback, i.e. an
 * audible dropout). Nothing else reports these; poll and diff to attribute
 * glitches to starvation rather than to decoding or the network. */
unsigned int board_audio_underrun_count(void);
esp_err_t board_display_draw_rgb565(int x1, int y1, int x2, int y2, const uint16_t *pixels);
/* The same rectangle from pixels already in the panel's wire order - swapped
 * with board_display_wire_pixel() - and straight from where they are, PSRAM
 * included: no band copy, one write. For a bitmap in PSRAM the buffer and its
 * size must be BOARD_DISPLAY_WIRE_ALIGN-aligned, or the SPI driver will try
 * to copy it into internal RAM. */
esp_err_t board_display_draw_wire(int x1, int y1, int x2, int y2, const uint16_t *pixels);
#define BOARD_DISPLAY_WIRE_ALIGN 64
/* A rectangle of one colour, given in wire order. */
esp_err_t board_display_fill(int x1, int y1, int x2, int y2, uint16_t wire_colour);
/* Shifts the whole picture along the panel's gate axis - screen x on a
 * landscape build, y on a portrait one - by `offset` pixels, in the
 * controller: it takes effect at the panel's next refresh, tear-free, and
 * costs the bus two bytes. What runs off one edge comes back in at the
 * other. 0 puts the picture back where it was written. */
esp_err_t board_display_scroll(int offset);
/* Native RGB565 pixels rewritten in place as the panel wants them on the
 * wire - a byte swap on the 16-bit panels, nothing on the converting ones. */
void board_display_to_wire(uint16_t *pixels, size_t count);
esp_err_t board_display_set_rotation(bool flip_vertical, bool flip_horizontal);

/* Loudest sample per channel since the previous call, then resets. Taking
 * rather than reading matters: PCM arrives a block at a time - 26 ms of MP3,
 * 93 ms of FLAC - while the UI polls every 10 ms, so a plain "current level"
 * would be sampled several times between blocks and would miss peaks whenever
 * the two rates drift.
 *
 * Returns false when no block has been written since the last call, which is
 * not the same as silence and must not be drawn as it: most passes fall
 * between two blocks, and on FLAC eight of every nine do. */
bool board_audio_level_take(uint16_t *left, uint16_t *right);

/* Playback volume, 0..100. The PCM5102 has no volume control, so this scales
 * the samples on their way to I2S - see audio_volume.h for what that costs.
 * 100 is bit-exact: the scaling is skipped rather than multiplied by one. */
void board_audio_set_volume(uint8_t percent);
uint8_t board_audio_volume(void);
