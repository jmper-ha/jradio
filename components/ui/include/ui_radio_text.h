#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Renders "<codec> | <bitrate> kbps | <rate>", the sample rate as a plain
 * value in Hz. Unknown bitrate and sample rate render as "--" so the line
 * keeps its shape while a stream is still being identified. */
void ui_radio_stream_text(char *text, size_t text_size, const char *codec,
                          uint16_t bitrate_kbps, uint32_t sample_rate_hz);
void ui_radio_stream_text_for_url(char *text, size_t text_size, const char *url);

/* Splits an ICY title into performer and track.
 *
 * Stations send one string, and the near-universal convention is
 * "Artist - Title". Showing it whole wastes the one wide line on the screen
 * and buries the track name in the middle of it. There is no way to be
 * certain, so the rule is conservative: split on the first " - " with spaces
 * on both sides, which a hyphenated word does not have. Anything else goes to
 * `title` untouched and leaves `artist` empty - a wrong split reads worse than
 * no split.
 *
 * Returns true when a split happened. */
bool ui_radio_split_title(const char *icy, char *artist, size_t artist_size, char *title,
                          size_t title_size);
