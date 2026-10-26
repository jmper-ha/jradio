#pragma once

#include <stddef.h>
#include <stdint.h>

/* Renders "<codec> | <bitrate> kbps | <rate>", the sample rate as a plain
 * value in Hz. Unknown bitrate and sample rate render as "--" so the line
 * keeps its shape while a stream is still being identified. */
void ui_radio_stream_text(char *text, size_t text_size, const char *codec,
                          uint16_t bitrate_kbps, uint32_t sample_rate_hz);

/* The same three readings stacked one per line instead of strung across one.
 * The narrow panel puts them in the column beside the cover art, where a
 * single line would have about 116 px and would be dotted before it reached
 * the sample rate. */
/* The same readings bare: spaces between them and the bitrate without its
 * unit - "MP3  128  44100" - for a panel whose line has room for nothing
 * more beside the footer. */
void ui_radio_stream_brief(char *text, size_t text_size, const char *codec,
                           uint16_t bitrate_kbps, uint32_t sample_rate_hz);
void ui_radio_stream_lines(char *text, size_t text_size, const char *codec,
                           uint16_t bitrate_kbps, uint32_t sample_rate_hz);
void ui_radio_stream_text_for_url(char *text, size_t text_size, const char *url);
