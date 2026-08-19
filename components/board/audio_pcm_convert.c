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
