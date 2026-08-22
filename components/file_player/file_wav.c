#include "file_wav.h"

#include <stdbool.h>
#include <string.h>

#define WAV_FORMAT_PCM 0x0001U
// WAVE_FORMAT_EXTENSIBLE: the real format lives in a sub-format GUID whose
// first two bytes repeat the format tag, which is all we need to check.
#define WAV_FORMAT_EXTENSIBLE 0xFFFEU

static uint16_t read_u16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t read_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

file_wav_result_t file_wav_parse_header(const uint8_t *data, size_t length, file_wav_info_t *info)
{
    if (data == NULL || info == NULL) return FILE_WAV_INVALID;
    if (length < 12U) return FILE_WAV_NEED_MORE_DATA;
    if (memcmp(data, "RIFF", 4U) != 0 || memcmp(data + 8U, "WAVE", 4U) != 0) {
        return FILE_WAV_INVALID;
    }

    memset(info, 0, sizeof(*info));
    bool have_format = false;
    size_t offset = 12U;
    while (offset + 8U <= length) {
        const uint8_t *id = data + offset;
        const uint32_t chunk_size = read_u32(data + offset + 4U);
        const size_t body = offset + 8U;
        // A chunk header claiming more than the file can hold is corruption,
        // not a short read: stop instead of asking for data that never comes.
        if (chunk_size > (uint32_t)SIZE_MAX - body) return FILE_WAV_INVALID;

        if (memcmp(id, "fmt ", 4U) == 0) {
            if (chunk_size < 16U) return FILE_WAV_INVALID;
            if (body + 16U > length) return FILE_WAV_NEED_MORE_DATA;
            uint16_t format_tag = read_u16(data + body);
            info->channels = read_u16(data + body + 2U);
            info->sample_rate = read_u32(data + body + 4U);
            info->bits_per_sample = read_u16(data + body + 14U);
            if (format_tag == WAV_FORMAT_EXTENSIBLE) {
                // cbSize + the first two bytes of the sub-format GUID.
                if (chunk_size < 26U) return FILE_WAV_INVALID;
                if (body + 26U > length) return FILE_WAV_NEED_MORE_DATA;
                format_tag = read_u16(data + body + 24U);
            }
            if (format_tag != WAV_FORMAT_PCM) return FILE_WAV_UNSUPPORTED;
            // The I2S channel is configured once, for 16-bit stereo slots.
            // 8- and 24-bit files would need a conversion pass that does not
            // exist, and refusing them beats playing noise.
            if (info->bits_per_sample != 16U) return FILE_WAV_UNSUPPORTED;
            if (info->channels != 1U && info->channels != 2U) return FILE_WAV_UNSUPPORTED;
            if (info->sample_rate == 0U) return FILE_WAV_INVALID;
            have_format = true;
        } else if (memcmp(id, "data", 4U) == 0) {
            // "fmt " is required to precede "data" by the specification, and
            // without it the samples cannot be interpreted.
            if (!have_format) return FILE_WAV_INVALID;
            info->data_offset = body;
            info->data_length = (size_t)chunk_size;
            return FILE_WAV_OK;
        }

        // Chunks are word-aligned: an odd size is followed by a pad byte that
        // is not counted in the size field.
        const size_t advance = (size_t)chunk_size + (chunk_size & 1U);
        if (advance > SIZE_MAX - body) return FILE_WAV_INVALID;
        offset = body + advance;
    }
    return FILE_WAV_NEED_MORE_DATA;
}
