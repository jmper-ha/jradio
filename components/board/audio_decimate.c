#include "audio_decimate.h"

#include <stdbool.h>
#include <string.h>

/* The halfband's taps beside the centre one, which is 0.5: every other tap
 * of a halfband is zero, and these are the odd ones, 1, 3 ... 31 from the
 * centre, the same on both sides. A windowed sinc at a quarter of the rate,
 * Kaiser beta 7.86, in Q15. The worst case of a sum - every pair at full
 * scale with its tap's sign - is 1.81e9, inside an int32. */
static const int32_t k_halfband[16] = {
    10391, -3359, 1895, -1233, 845, -589, 409, -279,
    186,   -119,  73,   -42,   22,  -10,  4,   -1,
};

#define HALFBAND_CENTRE ((AUDIO_DECIMATE_TAPS - 1U) / 2U)

unsigned audio_decimate_factor(uint32_t sample_rate)
{
    if (sample_rate > 96000U) return 4U;
    if (sample_rate > 48000U) return 2U;
    return 1U;
}

void audio_decimate_init(audio_decimate_t *decimate, unsigned factor)
{
    memset(decimate, 0, sizeof(*decimate));
    decimate->stages = factor >= 4U ? 2U : factor >= 2U ? 1U : 0U;
}

static int16_t saturate(int32_t sum)
{
    sum = (sum + 16384) >> 15;
    if (sum > INT16_MAX) return INT16_MAX;
    if (sum < INT16_MIN) return INT16_MIN;
    return (int16_t)sum;
}

// One frame in; true, with `pair` replaced by the output, on every second.
static bool stage_push(audio_decimate_stage_t *stage, int16_t pair[2])
{
    const unsigned pos = stage->pos;
    int16_t *line = stage->line;
    line[2U * pos] = pair[0];
    line[2U * pos + 1U] = pair[1];
    line[2U * (pos + AUDIO_DECIMATE_TAPS)] = pair[0];
    line[2U * (pos + AUDIO_DECIMATE_TAPS) + 1U] = pair[1];
    stage->pos = (uint8_t)(pos + 1U == AUDIO_DECIMATE_TAPS ? 0U : pos + 1U);
    stage->phase ^= 1U;
    if (stage->phase != 0U) return false;
    // After the advance `pos` is the oldest frame, and the centre is the
    // output's instant.
    const int16_t *centre = &line[2U * (stage->pos + HALFBAND_CENTRE)];
    int32_t left = (int32_t)centre[0] * 16384;
    int32_t right = (int32_t)centre[1] * 16384;
#pragma GCC unroll 16
    for (unsigned tap = 0U; tap < 16U; ++tap) {
        const int offset = 2 * (2 * (int)tap + 1);
        left += k_halfband[tap] * ((int32_t)centre[-offset] + (int32_t)centre[offset]);
        right += k_halfband[tap] * ((int32_t)centre[1 - offset] + (int32_t)centre[1 + offset]);
    }
    pair[0] = saturate(left);
    pair[1] = saturate(right);
    return true;
}

size_t audio_decimate_s16(audio_decimate_t *decimate, const int16_t *in, size_t frames,
                          int16_t *out)
{
    if (decimate->stages == 0U) {
        memcpy(out, in, frames * 2U * sizeof(int16_t));
        return frames;
    }
    size_t written = 0U;
    for (size_t frame = 0U; frame < frames; ++frame) {
        int16_t pair[2] = {in[2U * frame], in[2U * frame + 1U]};
        bool ready = true;
        for (unsigned index = 0U; index < decimate->stages && ready; ++index) {
            ready = stage_push(&decimate->stage[index], pair);
        }
        if (!ready) continue;
        out[2U * written] = pair[0];
        out[2U * written + 1U] = pair[1];
        ++written;
    }
    return written;
}
