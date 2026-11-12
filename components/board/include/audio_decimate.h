#pragma once

#include <stddef.h>
#include <stdint.h>

/* Stereo 16-bit PCM brought down by 2 or 4 for the S/PDIF output. The S3 has
 * no audio PLL, and its fractional divider puts every edge on a 6.25 ns grid:
 * at 96 kHz that is 8% of an 81 ns biphase cell, and the bench's receiver did
 * not lock to it, where 44.1 and 48 kHz played clean. So 88.2/96 kHz go out at
 * 44.1/48, and 176.4/192 kHz the same, through two stages.
 *
 * Each stage is a 63-tap Kaiser halfband: flat to 20 kHz at 96 kHz in, 77 dB
 * down by 27.8 kHz, where the band folding onto the audible one starts. In
 * integers: in float, even optimised and in internal RAM, it took 10.6% of a
 * core at 96 kHz, and the file player's FLAC had no time to spare for it. */

#define AUDIO_DECIMATE_TAPS 63U
#define AUDIO_DECIMATE_MAX_STAGES 2U

typedef struct {
    /* The last inputs as interleaved frames, written twice so a window never
     * wraps: both channels are summed in one pass over it. */
    int16_t line[2U * 2U * AUDIO_DECIMATE_TAPS];
    uint8_t pos;
    // Inputs taken since the last output, 0 or 1.
    uint8_t phase;
} audio_decimate_stage_t;

typedef struct {
    uint8_t stages;
    audio_decimate_stage_t stage[AUDIO_DECIMATE_MAX_STAGES];
} audio_decimate_t;

// 1, 2 or 4: the least that brings `sample_rate` to 48 kHz or under.
unsigned audio_decimate_factor(uint32_t sample_rate);

// For `factor` 1, 2 or 4, with silence behind it.
void audio_decimate_init(audio_decimate_t *decimate, unsigned factor);

/* `frames` interleaved stereo frames in, the output frames back - any count
 * in, the stages remembering where they are, so blocks may be split
 * anywhere. With n x factor frames in, never more than n come out. */
size_t audio_decimate_s16(audio_decimate_t *decimate, const int16_t *in, size_t frames,
                          int16_t *out);
