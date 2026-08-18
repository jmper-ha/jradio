#include "audio_volume.h"

/* Breakpoints of the volume curve: setting and the multiplier it maps to,
 * every ten percent worth 4 dB. Interpolated linearly in amplitude between
 * neighbours, which is close enough for a control read in whole percent.
 * Integer-only: no libm on the device, no -lm in the host tests. */
typedef struct {
    uint8_t percent;
    uint16_t gain;
} audio_volume_point_t;

static const audio_volume_point_t s_curve[] = {
    {100U, 32768U}, /*   0 dB */
    {90U, 20675U},  /*  -4 dB */
    {80U, 13045U},  /*  -8 dB */
    {70U, 8231U},   /* -12 dB */
    {60U, 5193U},   /* -16 dB */
    {50U, 3277U},   /* -20 dB */
    {40U, 2067U},   /* -24 dB */
    {30U, 1304U},   /* -28 dB */
    {20U, 823U},    /* -32 dB */
    {10U, 519U},    /* -36 dB */
    {1U, 328U},     /* -40 dB */
};

uint16_t audio_volume_gain(uint8_t percent)
{
    if (percent == 0U) return 0U;
    if (percent >= AUDIO_VOLUME_MAX_PERCENT) return AUDIO_VOLUME_UNITY;

    const size_t count = sizeof(s_curve) / sizeof(s_curve[0]);
    for (size_t index = 1U; index < count; ++index) {
        if (percent > s_curve[index].percent) {
            /* Between index-1 (louder) and index (quieter). */
            const audio_volume_point_t high = s_curve[index - 1U];
            const audio_volume_point_t low = s_curve[index];
            const uint32_t span = (uint32_t)(high.percent - low.percent);
            const uint32_t into = (uint32_t)(percent - low.percent);
            const uint32_t range = (uint32_t)(high.gain - low.gain);
            return (uint16_t)(low.gain + (into * range) / span);
        }
    }
    return s_curve[count - 1U].gain;
}

void audio_volume_apply(const uint8_t *source, uint8_t *destination, size_t length,
                        uint16_t gain)
{
    if (source == NULL || destination == NULL) return;

    size_t offset = 0U;
    for (; offset + 1U < length; offset += 2U) {
        const int16_t sample = (int16_t)((uint16_t)source[offset] |
                                         ((uint16_t)source[offset + 1U] << 8U));
        /* Round to nearest by adding half a step before the shift, away from
         * zero on both sides so the error does not bias towards DC. */
        int32_t scaled = (int32_t)sample * (int32_t)gain;
        scaled += scaled >= 0 ? (int32_t)(AUDIO_VOLUME_UNITY / 2U)
                              : -(int32_t)(AUDIO_VOLUME_UNITY / 2U);
        scaled /= (int32_t)AUDIO_VOLUME_UNITY;
        if (scaled > INT16_MAX) scaled = INT16_MAX;
        if (scaled < INT16_MIN) scaled = INT16_MIN;
        const uint16_t encoded = (uint16_t)(int16_t)scaled;
        destination[offset] = (uint8_t)encoded;
        destination[offset + 1U] = (uint8_t)(encoded >> 8U);
    }
    /* An odd trailing byte is half a sample; dropping it would shift every
     * following block by one byte and swap the channels. */
    if (offset < length) destination[offset] = source[offset];
}

uint8_t audio_volume_step(uint8_t percent, int step)
{
    const int next = (int)percent + step;
    if (next <= 0) return 0U;
    if (next >= (int)AUDIO_VOLUME_MAX_PERCENT) return (uint8_t)AUDIO_VOLUME_MAX_PERCENT;
    return (uint8_t)next;
}
