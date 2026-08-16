#include "pcm_diagnostics.h"

#include <stdbool.h>

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

static uint16_t sample_magnitude(const uint8_t *pcm, size_t offset)
{
    const int16_t sample = (int16_t)((uint16_t)pcm[offset] |
                                     ((uint16_t)pcm[offset + 1U] << 8U));
    return sample == INT16_MIN ? 32768U : (uint16_t)(sample < 0 ? -sample : sample);
}

void pcm_s16le_peak_stereo(const uint8_t *pcm, size_t length, uint16_t *left,
                           uint16_t *right)
{
    if (left == NULL || right == NULL) return;
    *left = 0U;
    *right = 0U;
    if (pcm == NULL) return;
    for (size_t offset = 0; offset + 3U < length; offset += 4U) {
        const uint16_t l = sample_magnitude(pcm, offset);
        const uint16_t r = sample_magnitude(pcm, offset + 2U);
        if (l > *left) *left = l;
        if (r > *right) *right = r;
    }
}

uint32_t pcm_s16le_zero_frame_run(const uint8_t *pcm, size_t length, uint32_t *carry)
{
    if (carry == NULL) {
        return 0U;
    }
    if (pcm == NULL) {
        return *carry;
    }

    uint32_t run = *carry;
    uint32_t longest = *carry;
    for (size_t offset = 0; offset + 3U < length; offset += 4U) {
        const bool silent = pcm[offset] == 0U && pcm[offset + 1U] == 0U &&
                            pcm[offset + 2U] == 0U && pcm[offset + 3U] == 0U;
        if (silent) {
            ++run;
            if (run > longest) {
                longest = run;
            }
        } else {
            run = 0U;
        }
    }
    *carry = run;
    return longest;
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
