#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Where a track has got to, and how long it is.
 *
 * The two halves come from different places on purpose.
 *
 * Elapsed is counted from the PCM the decoder has produced, not from how far
 * through the file the reader has got. The reader runs up to a full input
 * buffer ahead - two seconds at 128 kbps - so a file position would show a
 * time the listener has not heard yet, and would jump at the start where the
 * buffer fills at once. Decoded samples are what came out of the speaker.
 *
 * Total is estimated from the file size and the bitrate, because none of the
 * formats here is required to state its duration and parsing one out means
 * scanning the whole file first. That is exact for WAV and for constant
 * bitrate, and drifts on a variable-bitrate file - which is why the caller is
 * told when the estimate is unavailable rather than being handed a zero it
 * might display.
 */

/* Seconds of audio the decoder has actually produced. Zero until the format is
 * known, since the sample rate is what converts bytes to time. */
uint32_t usb_track_elapsed_seconds(uint64_t pcm_bytes, uint32_t sample_rate_hz,
                                   uint8_t channels, uint8_t bits_per_sample);

/* Seconds the file is expected to hold. `header_bytes` is what precedes the
 * audio - a WAV header, an ID3 tag - and is excluded so a short file is not
 * reported as longer than it is. Returns 0 when the bitrate is not known yet,
 * which is normal for the first moments of a track. */
uint32_t usb_track_total_seconds(uint64_t file_bytes, uint64_t header_bytes,
                                 uint16_t bitrate_kbps);

/* Position as 0..100 for a progress bar. False when there is nothing
 * meaningful to draw - no duration estimate yet, or an empty track - so the
 * caller can leave the bar alone instead of drawing a full or empty one that
 * means nothing.
 *
 * Clamped at 100: a variable-bitrate file routinely outlives the estimate, and
 * a bar that overshoots its track looks broken in a way that being pinned at
 * the end does not. */
bool usb_track_progress_percent(uint32_t elapsed_s, uint32_t total_s, uint8_t *percent);

/* "2:41", or "1:02:03" once a track passes the hour. Minutes are not padded
 * below ten but seconds always are, which is how every player writes it. */
void usb_track_time_text(char *text, size_t text_size, uint32_t seconds);
