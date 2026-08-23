#pragma once

#include <stddef.h>
#include <stdint.h>

/* Digital volume for a DAC that has no volume control of its own.
 *
 * The PCM5102 outputs at a fixed level, so the only place a volume knob can
 * exist is in the samples themselves. That has a real cost worth stating: each
 * 6 dB of attenuation throws away a bit of the sixteen, so at low settings the
 * noise floor rises in a way an analog control would not cause. Full volume is
 * therefore bit-exact - the scaling is skipped entirely rather than multiplied
 * by one - and the curve is built so ordinary listening sits high on it.
 *
 * The level meter must be measured *before* this is applied. It reports what
 * the station is sending, and a meter that dropped when the listener turned
 * the volume down would look exactly like a failing stream. */

/* Unity gain. Chosen as a power of two so applying it is a shift. */
#define AUDIO_VOLUME_UNITY 32768U
#define AUDIO_VOLUME_MAX_PERCENT 100U

/* Multiplier for a volume setting, 0..100 -> 0..AUDIO_VOLUME_UNITY.
 *
 * Logarithmic, spanning 40 dB: hearing is logarithmic, and a linear control
 * would put every useful setting in the top few percent and leave the rest of
 * the travel inaudible. 0 is exact silence rather than the bottom of the
 * curve, so the knob has a real off position. */
uint16_t audio_volume_gain(uint8_t percent);

/* Frames of fade-in when output starts, and the multiplier that fade is at
 * after `frames_done` of them.
 *
 * The DAC is handed silence until a source starts, so the first block is a
 * step from zero to wherever the decoder's first samples sit - and MP3 in
 * particular starts with a DC offset that the encoder never intended to be
 * heard. That step is the click at the start of playback. Twenty-odd
 * milliseconds of ramp removes it and is short enough that no one hears the
 * music arrive quietly.
 *
 * `frames_done` past the end of the fade returns `gain` unchanged, so the
 * caller can keep counting without a separate "still fading" flag. */
#define AUDIO_VOLUME_FADE_FRAMES 1024U /* 23 ms at 44.1 kHz */
/* 16-bit stereo, so four bytes to a frame. Stated here rather than taken from
 * the board options because these functions are pure and host-tested; the
 * board asserts the two agree. */
#define AUDIO_VOLUME_FRAME_BYTES 4U
uint16_t audio_volume_fade_gain(uint16_t gain, uint32_t frames_done, uint32_t fade_frames);

/* Scales interleaved 16-bit samples, moving the gain from `gain_start` to
 * `gain_end` across the block.
 *
 * The ramp is per frame rather than per block on purpose: a fade applied one
 * block at a time is a staircase, and each of its steps is the same
 * discontinuity the fade exists to remove - smaller, but a click is a click.
 */
void audio_volume_apply_ramp(const uint8_t *source, uint8_t *destination, size_t length,
                             uint16_t gain_start, uint16_t gain_end);

/* Scales interleaved 16-bit samples from `source` into `destination`.
 *
 * Separate buffers on purpose: the caller's block belongs to the decoder, and
 * the meter reads it after this runs. Rounds rather than truncating - at low
 * settings truncation is a DC-ward bias on every sample, which is audible as
 * distortion on quiet passages. A trailing odd byte is copied through. */
void audio_volume_apply(const uint8_t *source, uint8_t *destination, size_t length,
                        uint16_t gain);

/* Clamps a setting to the allowed range after a step, without wrapping.
 *
 * `step` may be negative and larger than the current value; the encoder can
 * deliver several clicks between polls, so this has to survive an overshoot in
 * either direction rather than assuming single steps. */
uint8_t audio_volume_step(uint8_t percent, int step);
