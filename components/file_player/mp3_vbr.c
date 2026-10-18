#include "mp3_vbr.h"

#include <string.h>

static uint32_t read_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
}

size_t mp3_id3v2_length(const uint8_t *data, size_t length)
{
    if (data == NULL || length < 10U || memcmp(data, "ID3", 3) != 0) return 0U;
    // The size is four 7-bit bytes; a set top bit means it is not a tag.
    if (((data[6] | data[7] | data[8] | data[9]) & 0x80U) != 0U) return 0U;
    const size_t size = ((size_t)data[6] << 21) | ((size_t)data[7] << 14) |
                        ((size_t)data[8] << 7) | (size_t)data[9];
    const bool footer = (data[5] & 0x10U) != 0U;
    return 10U + size + (footer ? 10U : 0U);
}

/* A Layer III frame header at `at`: the version (3 = MPEG-1, 2 = MPEG-2,
 * 0 = MPEG-2.5), the sample rate and whether the channel mode is mono. */
static bool frame_header(const uint8_t *at, uint32_t *version, uint32_t *sample_rate, bool *mono)
{
    static const uint32_t rates[3] = {44100U, 48000U, 32000U};
    const uint32_t header = read_be32(at);
    if ((header & 0xFFE00000U) != 0xFFE00000U) return false;
    const uint32_t ver = (header >> 19) & 3U;
    const uint32_t layer = (header >> 17) & 3U;
    const uint32_t bitrate = (header >> 12) & 15U;
    const uint32_t rate = (header >> 10) & 3U;
    if (ver == 1U || layer != 1U || bitrate == 0U || bitrate == 15U || rate == 3U) return false;
    *version = ver;
    *sample_rate = rates[rate] >> (ver == 3U ? 0U : ver == 2U ? 1U : 2U);
    *mono = ((header >> 6) & 3U) == 3U;
    return true;
}

bool mp3_vbr_parse(const uint8_t *data, size_t length, mp3_vbr_t *vbr)
{
    if (data == NULL || vbr == NULL) return false;
    memset(vbr, 0, sizeof(*vbr));
    for (size_t at = 0U; at + 4U <= length; ++at) {
        uint32_t version = 0U;
        uint32_t sample_rate = 0U;
        bool mono = false;
        if (!frame_header(data + at, &version, &sample_rate, &mono)) continue;
        // The side information the header sits behind: its size is fixed by
        // the version and the channel mode.
        const size_t side = version == 3U ? (mono ? 17U : 32U) : (mono ? 9U : 17U);
        const uint16_t samples = version == 3U ? 1152U : 576U;

        const size_t xing = at + 4U + side;
        if (xing + 8U <= length &&
            (memcmp(data + xing, "Xing", 4) == 0 || memcmp(data + xing, "Info", 4) == 0)) {
            const uint32_t flags = read_be32(data + xing + 4U);
            size_t field = xing + 8U;
            if ((flags & 1U) == 0U || field + 4U > length) return false;
            vbr->frames = read_be32(data + field);
            field += 4U;
            if ((flags & 2U) != 0U) {
                if (field + 4U > length) return false;
                vbr->bytes = read_be32(data + field);
                field += 4U;
            }
            if ((flags & 4U) != 0U && field + MP3_VBR_TOC_POINTS <= length) {
                memcpy(vbr->toc, data + field, MP3_VBR_TOC_POINTS);
                vbr->has_toc = true;
            }
        } else if (at + 4U + 32U + 18U <= length && memcmp(data + at + 36U, "VBRI", 4) == 0) {
            const size_t vbri = at + 36U;
            vbr->bytes = read_be32(data + vbri + 10U);
            vbr->frames = read_be32(data + vbri + 14U);
        } else {
            // The first frame carries no header: a constant-rate file, or one
            // whose encoder wrote none.
            return false;
        }
        if (vbr->frames == 0U) return false;
        vbr->sample_rate = sample_rate;
        vbr->samples_per_frame = samples;
        vbr->frame_offset = at;
        return true;
    }
    return false;
}

uint64_t mp3_vbr_duration_ms(const mp3_vbr_t *vbr)
{
    if (vbr == NULL || vbr->frames == 0U || vbr->sample_rate == 0U) return 0U;
    return (uint64_t)vbr->frames * vbr->samples_per_frame * 1000U / vbr->sample_rate;
}

uint64_t mp3_vbr_seek_offset(const mp3_vbr_t *vbr, uint64_t target_ms)
{
    const uint64_t total = mp3_vbr_duration_ms(vbr);
    if (total == 0U || vbr->bytes == 0U) return 0U;
    if (target_ms >= total) return vbr->bytes;
    if (!vbr->has_toc) return target_ms * vbr->bytes / total;
    /* The table gives, for each whole percent of the time, where it starts in
     * 256ths of the bytes; between two points the place is taken in line. In
     * thousandths of a percent, to keep it integer. */
    const uint64_t milli_percent = target_ms * 100000U / total;
    const uint32_t point = (uint32_t)(milli_percent / 1000U);
    const uint32_t from = vbr->toc[point];
    const uint32_t to = point + 1U < MP3_VBR_TOC_POINTS ? vbr->toc[point + 1U] : 256U;
    const uint64_t scaled = (uint64_t)from * 1000U +
                            (uint64_t)(to > from ? to - from : 0U) * (milli_percent % 1000U);
    return scaled * vbr->bytes / 256000U;
}
