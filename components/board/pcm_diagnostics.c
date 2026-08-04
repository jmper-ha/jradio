#include "pcm_diagnostics.h"

uint16_t pcm_s16le_peak(const uint8_t *pcm, size_t length)
{
    uint16_t peak = 0;

    if (pcm == NULL) {
        return 0;
    }
    for (size_t offset = 0; offset + 1U < length; offset += 2U) {
        const int16_t sample = (int16_t)((uint16_t)pcm[offset] |
                                         ((uint16_t)pcm[offset + 1U] << 8U));
        const uint16_t magnitude = sample == INT16_MIN ? 32768U :
                                   (uint16_t)(sample < 0 ? -sample : sample);
        if (magnitude > peak) {
            peak = magnitude;
        }
    }
    return peak;
}

size_t pcm_s16le_stereo_square_fill(uint8_t *pcm, size_t length, uint32_t sample_rate,
                                    uint32_t frequency, int16_t amplitude, uint32_t *phase)
{
    if (pcm == NULL || phase == NULL || sample_rate == 0U || frequency == 0U) {
        return 0;
    }

    const size_t frames = length / 4U;
    for (size_t frame = 0; frame < frames; ++frame) {
        const int16_t sample = *phase < (sample_rate / 2U) ? amplitude : (int16_t)-amplitude;
        const uint16_t encoded = (uint16_t)sample;
        const size_t offset = frame * 4U;
        pcm[offset] = (uint8_t)encoded;
        pcm[offset + 1U] = (uint8_t)(encoded >> 8U);
        pcm[offset + 2U] = (uint8_t)encoded;
        pcm[offset + 3U] = (uint8_t)(encoded >> 8U);
        *phase = (uint32_t)(((uint64_t)*phase + frequency) % sample_rate);
    }
    return frames * 4U;
}
