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
uint32_t file_track_elapsed_seconds(uint64_t pcm_bytes, uint32_t sample_rate_hz,
                                   uint8_t channels, uint8_t bits_per_sample);

/* Seconds the file is expected to hold. `header_bytes` is what precedes the
 * audio - a WAV header, an ID3 tag - and is excluded so a short file is not
 * reported as longer than it is. Returns 0 when the bitrate is not known yet,
 * which is normal for the first moments of a track. */
uint32_t file_track_total_seconds(uint64_t file_bytes, uint64_t header_bytes,
                                 uint16_t bitrate_kbps);

/* Length of a FLAC track, which cannot be had the way the line above has it.
 * The format is variable rate: its frames are as large as the music in them
 * needs, so a file size and an average say nothing about where in the track a
 * given byte falls. STREAMINFO carries the exact sample count instead, and
 * this is the division that turns it into seconds. */
uint32_t file_track_sampled_seconds(uint64_t total_samples, uint32_t sample_rate_hz);

/* The rate the file works out to over its whole length, for a format that
 * never states one.
 *
 * Wanted for two things, neither of which needs it to be exact: the codec line
 * on screen, where an average is the usual way to describe a FLAC, and the
 * jump arithmetic, which turns seconds into a byte offset. A jump lands
 * wherever the local rate differs from the average, and the decoder resyncs on
 * the next frame header regardless - so this is an aim, not a promise. */
uint16_t file_track_average_bitrate_kbps(uint64_t file_bytes, uint64_t header_bytes,
                                        uint32_t seconds);

/* Position as 0..100 for a progress bar. False when there is nothing
 * meaningful to draw - no duration estimate yet, or an empty track - so the
 * caller can leave the bar alone instead of drawing a full or empty one that
 * means nothing.
 *
 * Clamped at 100: a variable-bitrate file routinely outlives the estimate, and
 * a bar that overshoots its track looks broken in a way that being pinned at
 * the end does not. */
bool file_track_progress_percent(uint32_t elapsed_s, uint32_t total_s, uint8_t *percent);

/* Where in the file the audio for `target_seconds` starts, as an offset from
 * the start of the file.
 *
 * The inverse of file_track_total_seconds(), and inexact in exactly the same
 * way: on a variable-bitrate file the landing point drifts from the time asked
 * for by however much the average bitrate misses the local one. That is the
 * same estimate the progress bar is already drawn from, so the bar and the
 * jump agree with each other even where both are approximate.
 *
 * Returns `header_bytes` when the bitrate is not known yet - the start of the
 * audio is the only offset that is certainly safe to seek to. Never lands past
 * the end of the file: a seek beyond the last byte would end the track, which
 * is a jump nobody asked for. */
uint64_t file_track_seek_offset(uint64_t file_bytes, uint64_t header_bytes,
                               uint16_t bitrate_kbps, uint32_t target_seconds);

/* How much PCM the output has to have produced to be `seconds` into a track -
 * the inverse of file_track_elapsed_seconds(), so that after a jump the
 * position readout carries on from where the listener actually is instead of
 * counting up from zero again. */
uint64_t file_track_pcm_bytes(uint32_t seconds, uint32_t sample_rate_hz,
                             uint8_t channels, uint8_t bits_per_sample);

/* "2:41", or "1:02:03" once a track passes the hour. Minutes are not padded
 * below ten but seconds always are, which is how every player writes it. */
void file_track_time_text(char *text, size_t text_size, uint32_t seconds);

/* ---- tracks of a .cue sheet inside one file -------------------------------
 *
 * A sheet says where each track starts in CD frames (75 to the second); the
 * player counts samples. These turn one into the other and say which track a
 * position is in, so the player can play a whole disc side straight through
 * and still name the track on the air. */

// CD frames to samples at `sample_rate_hz`, exactly: 1/75 s is a whole number
// of samples at 44.1 and 48 kHz, and the rounding is down otherwise.
uint64_t file_track_cd_frames_to_samples(uint32_t cd_frames, uint32_t sample_rate_hz);

/* Which track `position_samples` falls in: the last whose start it has
 * reached. `starts_frames` is ascending. 0 before the first start, which is
 * where the first track's pregap is. */
size_t file_track_cue_track_at(const uint32_t *starts_frames, size_t count,
                               uint64_t position_samples, uint32_t sample_rate_hz);

/* ---- landing a FLAC jump on a sample --------------------------------------
 *
 * A jump by bitrate lands within a couple of seconds - fine for a scrub bar,
 * not for the start of a track, where it means hearing the tail of the one
 * before. FLAC frame headers say which sample they start at, so the jump can
 * aim by the file's own sample count, see where it landed, step back if it
 * went too far, and drop the samples between the frame and the target. */

/* Where sample `target` should be, by the average over the audio bytes. Never
 * before `audio_start` or past the end. */
uint64_t file_track_flac_aim(uint64_t audio_start, uint64_t file_bytes, uint64_t total_samples,
                             uint64_t target);

/* Where to aim again after landing on a frame that starts `overshoot` samples
 * past the target: back by that many samples' worth of bytes and one block
 * more, since a frame header only follows the offset. */
uint64_t file_track_flac_back_off(uint64_t offset, uint64_t audio_start, uint64_t file_bytes,
                                  uint64_t total_samples, uint64_t overshoot,
                                  uint32_t block_samples);
