#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t board_init(void);
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
esp_err_t board_display_set_rotation(bool flip_vertical, bool flip_horizontal);
