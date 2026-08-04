#include "mp3_stream_info.h"

uint16_t mp3_stream_bitrate_kbps(const uint8_t *data, size_t length)
{
    static const uint16_t mpeg1_layer3[] = {
        0U, 32U, 40U, 48U, 56U, 64U, 80U, 96U,
        112U, 128U, 160U, 192U, 224U, 256U, 320U, 0U,
    };
    static const uint16_t mpeg2_layer3[] = {
        0U, 8U, 16U, 24U, 32U, 40U, 48U, 56U,
        64U, 80U, 96U, 112U, 128U, 144U, 160U, 0U,
    };

    if (data == NULL || length < 4U) {
        return 0U;
    }
    for (size_t offset = 0; offset + 3U < length; ++offset) {
        if (data[offset] != 0xffU || (data[offset + 1U] & 0xe0U) != 0xe0U) {
            continue;
        }
        const uint8_t version = (data[offset + 1U] >> 3U) & 0x03U;
        const uint8_t layer = (data[offset + 1U] >> 1U) & 0x03U;
        const uint8_t bitrate_index = data[offset + 2U] >> 4U;
        const uint8_t sample_rate_index = (data[offset + 2U] >> 2U) & 0x03U;
        if (version == 1U || layer != 1U || bitrate_index == 0U || bitrate_index == 15U ||
            sample_rate_index == 3U) {
            continue;
        }
        return version == 3U ? mpeg1_layer3[bitrate_index] : mpeg2_layer3[bitrate_index];
    }
    return 0U;
}
