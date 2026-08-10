#pragma once

#include <stddef.h>
#include <stdint.h>

/* Renders "<codec> | <bitrate> kbps | <rate>", the sample rate as a plain
 * value in Hz. Unknown bitrate and sample rate render as "--" so the line
 * keeps its shape while a stream is still being identified. */
void ui_radio_stream_text(char *text, size_t text_size, const char *codec,
                          uint16_t bitrate_kbps, uint32_t sample_rate_hz);
void ui_radio_stream_text_for_url(char *text, size_t text_size, const char *url);
