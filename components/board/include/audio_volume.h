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
