#pragma once

#include <stddef.h>
#include <stdint.h>

uint16_t pcm_s16le_peak(const uint8_t *pcm, size_t length);

/* Longest run of consecutive all-zero stereo frames, in frames.
 *
 * The PCM5102 engages its analog mute after 1024 consecutive zero frames
 * (~23 ms at 44.1 kHz), so this is the exact quantity the DAC itself reacts to
 * - and it is not visible in a per-block peak, because a run can straddle two
 * blocks while leaving neither of them entirely zero. `carry` holds the run
 * still open at the end of the previous block and is updated on return, so
 * runs spanning any number of blocks are measured whole. */
uint32_t pcm_s16le_zero_frame_run(const uint8_t *pcm, size_t length, uint32_t *carry);
size_t pcm_s16le_stereo_square_fill(uint8_t *pcm, size_t length, uint32_t sample_rate,
                                    uint32_t frequency, int16_t amplitude, uint32_t *phase);
