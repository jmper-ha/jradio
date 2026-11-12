#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* S/PDIF (IEC 60958, consumer) made in software and sent as an I2S stream.
 * A stereo frame is two subframes of 32 time slots, each slot two cells of
 * biphase mark: 128 cells. I2S in 32-bit stereo slots at twice the audio rate
 * clocks out exactly that many bits per audio frame, so each 16-bit frame
 * becomes four 32-bit words - left subframe, then right, earliest cell in the
 * word's top bit, which is the one I2S sends first. */

#define AUDIO_SPDIF_WORDS_PER_FRAME 4U
#define AUDIO_SPDIF_BLOCK_FRAMES 192U

typedef struct {
    // Where the 192-frame channel-status block is; 0 takes the B preamble.
    uint8_t frame;
    // Channel status, the same for both channels: bit n of the block is
    // bit (n % 8) of status[n / 8].
    uint8_t status[AUDIO_SPDIF_BLOCK_FRAMES / 8U];
} audio_spdif_encoder_t;

// Consumer PCM, copying permitted, and the rate's code; 0 for a rate the
// standard has no code for, which receivers treat as "not indicated".
void audio_spdif_init(audio_spdif_encoder_t *encoder, uint32_t sample_rate);

/* `frames` stereo frames of interleaved 16-bit samples into
 * AUDIO_SPDIF_WORDS_PER_FRAME words each; NULL `samples` encodes silence,
 * which keeps a receiver locked where a stopped line would not. The encoder
 * carries the block position from one call to the next. */
void audio_spdif_encode_s16(audio_spdif_encoder_t *encoder, const int16_t *samples,
                            size_t frames, uint32_t *words);

// The rate's code for channel-status bits 24-27, as bits 0-3; 0xFF when none.
uint8_t audio_spdif_rate_code(uint32_t sample_rate);
