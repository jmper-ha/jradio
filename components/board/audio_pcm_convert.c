#include "audio_pcm_convert.h"

#include <string.h>

/* One 16-bit sample. Samples are moved as bytes rather than through an
 * int16_t *, the same as audio_volume_apply: the callers hand over uint8_t
 * buffers from heap_caps_malloc and decoder scratch, and byte moves carry no
 * alignment or endianness assumption about either end. */
#define AUDIO_PCM_SAMPLE_BYTES 2U

size_t audio_pcm_stereo_bytes(size_t source_bytes, uint8_t channels)
{
    if (channels != 1U && channels != 2U) return 0U;
    const size_t frame_bytes = (size_t)channels * AUDIO_PCM_SAMPLE_BYTES;
    if (source_bytes % frame_bytes != 0U) return 0U;
    return channels == 1U ? source_bytes * 2U : source_bytes;
}

bool audio_pcm_to_stereo_s16(const uint8_t *source, size_t source_bytes, uint8_t channels,
                             uint8_t *destination, size_t destination_capacity,
                             size_t *written)
{
    if (source == NULL || destination == NULL || written == NULL) return false;
    if (channels != 1U && channels != 2U) return false;

    const size_t frame_bytes = (size_t)channels * AUDIO_PCM_SAMPLE_BYTES;
    /* A partial frame means the caller's framing is already wrong. Dropping
     * the remainder would swap the channels for the rest of the track, so this
     * refuses instead of guessing. */
    if (source_bytes % frame_bytes != 0U) return false;

    const size_t output_bytes = channels == 1U ? source_bytes * 2U : source_bytes;
    /* Overflow only reachable from a caller that already lost track of its own
     * buffer, but the multiply above is the one place it could wrap. */
    if (channels == 1U && output_bytes / 2U != source_bytes) return false;
    if (output_bytes > destination_capacity) return false;

    if (channels == 2U) {
        memcpy(destination, source, source_bytes);
        *written = source_bytes;
        return true;
    }

    for (size_t offset = 0U; offset < source_bytes; offset += AUDIO_PCM_SAMPLE_BYTES) {
        const size_t out = offset * 2U;
        destination[out] = source[offset];
        destination[out + 1U] = source[offset + 1U];
        destination[out + 2U] = source[offset];
        destination[out + 3U] = source[offset + 1U];
    }
    *written = output_bytes;
    return true;
}

bool audio_pcm_mono_to_stereo_inplace_s16(uint8_t *buffer, size_t mono_bytes,
                                          size_t capacity, size_t *written)
{
    if (buffer == NULL || written == NULL) return false;
    if (mono_bytes % AUDIO_PCM_SAMPLE_BYTES != 0U) return false;

    const size_t output_bytes = mono_bytes * 2U;
    if (output_bytes / 2U != mono_bytes) return false;
    if (output_bytes > capacity) return false;

    /* Backwards: the sample read at `offset` is written to `offset * 2` and
     * above, which is never below `offset`, so every sample still to be read
     * lies below everything already written. */
    size_t offset = mono_bytes;
    while (offset > 0U) {
        offset -= AUDIO_PCM_SAMPLE_BYTES;
        const uint8_t low = buffer[offset];
        const uint8_t high = buffer[offset + 1U];
        const size_t out = offset * 2U;
        buffer[out] = low;
        buffer[out + 1U] = high;
        buffer[out + 2U] = low;
        buffer[out + 3U] = high;
    }
    *written = output_bytes;
    return true;
}

/* Bytes per sample for a depth this can narrow. Zero rejects the depth. */
static size_t narrow_source_sample_bytes(uint8_t bits_per_sample)
{
    if (bits_per_sample == 16U) return 2U;
    if (bits_per_sample == 24U) return 3U;
    if (bits_per_sample == 32U) return 4U;
    return 0U;
}

static int16_t read_s16(const uint8_t *at)
{
    return (int16_t)((uint16_t)at[0] | ((uint16_t)at[1] << 8U));
}

static void write_s16(uint8_t *at, int32_t value)
{
    const uint16_t bits = (uint16_t)(int16_t)value;
    at[0] = (uint8_t)(bits & 0xFFU);
    at[1] = (uint8_t)(bits >> 8U);
}

bool audio_pcm_upsample_s16(const uint8_t *source, size_t source_bytes, uint32_t factor,
                            const int16_t previous[2], uint8_t *destination,
                            size_t destination_capacity)
{
    if (source == NULL || destination == NULL || previous == NULL || factor == 0U ||
        source_bytes % 4U != 0U || source_bytes > destination_capacity / factor) {
        return false;
    }
    int32_t last[2] = {previous[0], previous[1]};
    uint8_t *out = destination;
    for (size_t at = 0U; at < source_bytes; at += 4U) {
        const int32_t next[2] = {read_s16(source + at), read_s16(source + at + 2U)};
        for (uint32_t step = 1U; step <= factor; ++step) {
            // Between two 16-bit samples, so never past either: no clipping.
            for (unsigned channel = 0U; channel < 2U; ++channel) {
                const int32_t value =
                    last[channel] + (next[channel] - last[channel]) * (int32_t)step / (int32_t)factor;
                write_s16(out + channel * 2U, value);
            }
            out += 4U;
        }
        last[0] = next[0];
        last[1] = next[1];
    }
    return true;
}

size_t audio_pcm_narrowed_bytes(size_t source_bytes, uint8_t bits_per_sample)
{
    const size_t sample_bytes = narrow_source_sample_bytes(bits_per_sample);
    if (sample_bytes == 0U) return 0U;
    if (source_bytes % sample_bytes != 0U) return 0U;
    return source_bytes / sample_bytes * AUDIO_PCM_SAMPLE_BYTES;
}

bool audio_pcm_narrow_to_s16(uint8_t *buffer, size_t source_bytes, uint8_t bits_per_sample,
                             size_t *written)
{
    if (buffer == NULL || written == NULL) return false;
    const size_t sample_bytes = narrow_source_sample_bytes(bits_per_sample);
    if (sample_bytes == 0U) return false;
    /* A partial sample means the caller's framing is already wrong; carrying
     * on would shift every sample after it by a byte. */
    if (source_bytes % sample_bytes != 0U) return false;

    if (sample_bytes == AUDIO_PCM_SAMPLE_BYTES) {
        *written = source_bytes;
        return true;
    }

    /* Forwards: sample `n` is read at `n * sample_bytes` and written at
     * `n * 2`, which is never above it, so nothing still to be read has been
     * overwritten. Little-endian, so the two most significant bytes are the
     * last two of the sample. */
    size_t out = 0U;
    for (size_t offset = 0U; offset < source_bytes; offset += sample_bytes) {
        buffer[out] = buffer[offset + sample_bytes - 2U];
        buffer[out + 1U] = buffer[offset + sample_bytes - 1U];
        out += AUDIO_PCM_SAMPLE_BYTES;
    }
    *written = out;
    return true;
}
