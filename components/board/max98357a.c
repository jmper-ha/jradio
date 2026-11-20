#include "max98357a.h"
#include "audio_pcm_convert.h"
#include "audio_volume.h"
#include <math.h>
#include <string.h>

bool max98357a_rate_plan(uint32_t hz, max98357a_rate_t *p)
{
    if (p == NULL) return false;
    max98357a_rate_t r = {.bus_hz = hz, .up = 1, .down = 1};
    switch (hz) {
    case 8000: case 11025: case 12000: r.up = 4; break;
    case 16000: case 22050: case 24000: r.up = 2; break;
    case 32000: case 44100: case 48000: break;
    case 64000: case 88200: case 96000: r.down = 2; break;
    case 128000: case 176400: case 192000: r.down = 4; break;
    default: return false;
    }
    r.bus_hz = hz * r.up / r.down;
    *p = r;
    return true;
}

uint16_t max98357a_ceiling_gain(int dbfs)
{
    if (dbfs > 0 || dbfs < -60) return 0;
    if (dbfs == 0) return AUDIO_VOLUME_UNITY;
    return (uint16_t)floor(32768.0 * pow(10.0, (double)dbfs / 20.0));
}

void max98357a_output_reset(max98357a_output_t *o, max98357a_rate_t rate,
                            uint16_t gain)
{
    memset(o, 0, sizeof(*o));
    o->rate = rate;
    o->gain = gain;
    audio_decimate_init(&o->decimator, rate.down);
}

size_t max98357a_output_capacity(size_t bytes, unsigned up)
{
    if (bytes == 0 || bytes % 4 != 0 || (up != 1 && up != 2 && up != 4) ||
        bytes > SIZE_MAX / up) return 0;
    return bytes * up;
}

bool max98357a_output_prepare(max98357a_output_t *o, const uint8_t *input,
                              size_t bytes, uint8_t *buffer, size_t capacity)
{
    const size_t need = max98357a_output_capacity(bytes, o->rate.up);
    if (o->pending_bytes || input == NULL || buffer == NULL || !need || capacity < need)
        return false;
    size_t made = bytes;
    if (o->rate.up > 1) {
        if (!audio_pcm_upsample_s16(input, bytes, o->rate.up, o->previous, buffer, capacity))
            return false;
        made *= o->rate.up;
    } else if (o->rate.down > 1) {
        made = 4 * audio_decimate_s16(&o->decimator, (const int16_t *)input, bytes / 4,
                                       (int16_t *)buffer);
    } else {
        memcpy(buffer, input, bytes);
    }
    memcpy(o->previous, input + bytes - 4, 4);
    /* After every DSP operation, once. Negative full scale stays within the
     * same absolute bound as positive full scale (8230 for -12 dBFS). */
    if (o->gain != AUDIO_VOLUME_UNITY) audio_volume_apply(buffer, buffer, made, o->gain);
    o->pending_at = 0;
    o->pending_bytes = made;
    return true;
}

bool max98357a_output_advance(max98357a_output_t *o, size_t sent)
{
    if (sent > o->pending_bytes) return false;
    o->pending_at += sent;
    o->pending_bytes -= sent;
    return true;
}
