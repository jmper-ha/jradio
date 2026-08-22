#include "flac_tags.h"

#include <string.h>

static uint32_t read_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

bool flac_signature_matches(const uint8_t *bytes, size_t length)
{
    return bytes != NULL && length >= FLAC_SIGNATURE_SIZE && memcmp(bytes, "fLaC", 4U) == 0;
}

/* CRC-8 over a frame header, polynomial x^8 + x^2 + x + 1 as the format
 * specifies. Computed bitwise rather than from a table: it runs over a dozen
 * bytes at a time, and a 256-byte table would be most of what this file
 * weighs. */
static uint8_t frame_header_crc8(const uint8_t *data, size_t length)
{
    uint8_t crc = 0U;
    for (size_t index = 0U; index < length; ++index) {
        crc ^= data[index];
        for (unsigned int bit = 0U; bit < 8U; ++bit) {
            const unsigned int shifted = (unsigned int)crc << 1;
            crc = (uint8_t)((crc & 0x80U) != 0U ? (shifted ^ 0x07U) : shifted);
        }
    }
    return crc;
}

// Length of the UTF-8-style run that carries the frame or sample number, from
// its first byte. Zero for a byte that cannot start one.
static size_t coded_number_length(uint8_t first)
{
    if ((first & 0x80U) == 0x00U) return 1U;
    if ((first & 0xE0U) == 0xC0U) return 2U;
    if ((first & 0xF0U) == 0xE0U) return 3U;
    if ((first & 0xF8U) == 0xF0U) return 4U;
    if ((first & 0xFCU) == 0xF8U) return 5U;
    if ((first & 0xFEU) == 0xFCU) return 6U;
    if (first == 0xFEU) return 7U;
    return 0U;
}

static uint32_t block_size_for(uint8_t code)
{
    if (code == 1U) return 192U;
    if (code <= 5U) return 576U << (code - 2U);
    // 6 and 7 state the size at the end of the header instead; the caller
    // reads it from there.
    if (code <= 7U) return 0U;
    return 256U << (code - 8U);
}

static uint32_t sample_rate_for(uint8_t code)
{
    static const uint32_t rates[12] = {0U,     88200U, 176400U, 192000U, 8000U,  16000U,
                                       22050U, 24000U, 32000U,  44100U,  48000U, 96000U};
    return code < 12U ? rates[code] : 0U;
}

bool flac_frame_header_parse(const uint8_t *data, size_t length, flac_frame_header_t *out)
{
    if (data == NULL || out == NULL || length < 5U) return false;
    /* Sync is fourteen bits, and the fifteenth is reserved and must be zero -
     * so 0xFA and 0xFB, which carry the same sync, are not frame headers. The
     * sixteenth is the blocking strategy and may be either. */
    if (data[0] != 0xFFU || (data[1] & 0xFEU) != 0xF8U) return false;

    const uint8_t block_code = (uint8_t)(data[2] >> 4);
    const uint8_t rate_code = (uint8_t)(data[2] & 0x0FU);
    const uint8_t channel_code = (uint8_t)(data[3] >> 4);
    const uint8_t depth_code = (uint8_t)((data[3] >> 1) & 0x07U);
    // Every reserved value the format defines, in one place: a header using
    // one is not a header, it is audio that happens to start with 0xFF.
    if (block_code == 0U || rate_code == 0x0FU || channel_code > 10U || depth_code == 3U ||
        depth_code == 7U || (data[3] & 0x01U) != 0U) {
        return false;
    }

    const size_t number_length = coded_number_length(data[4]);
    if (number_length == 0U) return false;
    size_t offset = 4U + number_length;
    uint32_t block_size = block_size_for(block_code);
    uint32_t sample_rate = sample_rate_for(rate_code);

    if (block_code == 6U || block_code == 7U) {
        const size_t width = block_code == 6U ? 1U : 2U;
        if (offset + width > length) return false;
        block_size = width == 1U ? (uint32_t)data[offset] + 1U
                                 : (((uint32_t)data[offset] << 8) | data[offset + 1U]) + 1U;
        offset += width;
    }
    if (rate_code >= 12U) {
        const size_t width = rate_code == 12U ? 1U : 2U;
        if (offset + width > length) return false;
        const uint32_t stated = width == 1U
                                    ? (uint32_t)data[offset]
                                    : (((uint32_t)data[offset] << 8) | data[offset + 1U]);
        // 12 is kilohertz, 13 is hertz, 14 is tens of hertz.
        sample_rate = rate_code == 12U ? stated * 1000U : (rate_code == 13U ? stated : stated * 10U);
        offset += width;
    }
    if (offset + 1U > length) return false;
    if (frame_header_crc8(data, offset) != data[offset]) return false;

    out->header_bytes = offset + 1U;
    out->block_size = block_size;
    out->sample_rate_hz = sample_rate;
    // 0-7 are that many channels plus one; 8, 9 and 10 are the stereo
    // decorrelations, all of which are two channels.
    out->channels = channel_code < 8U ? (uint8_t)(channel_code + 1U) : 2U;
    return true;
}

// Whether a header that parsed could belong to this stream.
static bool frame_matches_stream(const flac_frame_header_t *header,
                                 const flac_streaminfo_t *info)
{
    if (info == NULL) return true;
    // A frame may defer either figure to STREAMINFO, which agrees by
    // definition; when it states one, it has to be the same one.
    if (header->sample_rate_hz != 0U && header->sample_rate_hz != info->sample_rate_hz) {
        return false;
    }
    if (header->channels != info->channels) return false;
    /* No floor to go with it: the last frame of a file is as short as the
     * music left over - twenty samples in one of the tracks this was tested
     * against - and a minimum that looked reasonable threw exactly that frame
     * away. */
    return info->max_block_size == 0U || header->block_size <= info->max_block_size;
}

bool flac_frame_find_sync(const uint8_t *data, size_t length,
                          const flac_streaminfo_t *info, size_t *offset)
{
    if (data == NULL || offset == NULL) return false;
    for (size_t index = 0U; index + 5U <= length; ++index) {
        if (data[index] != 0xFFU) continue;
        flac_frame_header_t header;
        if (!flac_frame_header_parse(data + index, length - index, &header)) continue;
        if (!frame_matches_stream(&header, info)) continue;
        *offset = index;
        return true;
    }
    return false;
}

bool flac_streaminfo_parse(const uint8_t *block, size_t length, flac_streaminfo_t *out)
{
    if (block == NULL || out == NULL || length < FLAC_STREAMINFO_SIZE) return false;

    /* Bit-packed rather than byte-aligned, from byte 10: 20 bits of sample
     * rate, 3 of channel count less one, 5 of depth less one, then 36 bits of
     * sample count. The last of those is why this is read by hand - it
     * straddles five bytes and does not fit the file's own byte boundaries. */
    out->sample_rate_hz = ((uint32_t)block[10] << 12) | ((uint32_t)block[11] << 4) |
                          ((uint32_t)block[12] >> 4);
    out->channels = (uint8_t)(((block[12] >> 1) & 0x07U) + 1U);
    out->bits_per_sample =
        (uint8_t)((((uint32_t)(block[12] & 0x01U) << 4) | ((uint32_t)block[13] >> 4)) + 1U);
    out->total_samples = ((uint64_t)(block[13] & 0x0FU) << 32) | ((uint64_t)block[14] << 24) |
                         ((uint64_t)block[15] << 16) | ((uint64_t)block[16] << 8) |
                         (uint64_t)block[17];
    // The first four bytes, and the only byte-aligned fields in the block.
    out->min_block_size = (uint16_t)(((uint16_t)block[0] << 8) | block[1]);
    out->max_block_size = (uint16_t)(((uint16_t)block[2] << 8) | block[3]);

    // A rate of zero is not a FLAC file; the format forbids it, and every
    // duration computed from it would divide by it.
    return out->sample_rate_hz != 0U;
}

bool flac_block_header_parse(const uint8_t *header, size_t length, bool *last, uint8_t *type,
                             size_t *block_length)
{
    if (header == NULL || last == NULL || type == NULL || block_length == NULL) return false;
    if (length < FLAC_BLOCK_HEADER_SIZE) return false;
    *last = (header[0] & 0x80U) != 0U;
    *type = header[0] & 0x7FU;
    *block_length =
        ((size_t)header[1] << 16) | ((size_t)header[2] << 8) | (size_t)header[3];
    // 127 is explicitly invalid, and nothing else identifies a corrupt walk
    // before it has already seeked into the audio.
    return *type != 0x7FU;
}

static bool key_matches(const uint8_t *entry, size_t length, const char *key)
{
    const size_t key_length = strlen(key);
    if (length < key_length + 1U) return false;
    if (entry[key_length] != '=') return false;
    for (size_t index = 0U; index < key_length; ++index) {
        uint8_t value = entry[index];
        if (value >= 'a' && value <= 'z') value = (uint8_t)(value - 'a' + 'A');
        if (value != (uint8_t)key[index]) return false;
    }
    return true;
}

bool flac_vorbis_comment_parse(const uint8_t *block, size_t length, audio_tags_t *tags)
{
    if (block == NULL || tags == NULL || length < 8U) return false;
    const size_t vendor_length = read_le32(block);
    if (vendor_length > length - 8U) return false;
    size_t offset = 4U + vendor_length;
    const uint32_t count = read_le32(block + offset);
    offset += 4U;

    bool found_any = false;
    for (uint32_t index = 0U; index < count; ++index) {
        if (offset + 4U > length) break;
        const size_t entry_length = read_le32(block + offset);
        offset += 4U;
        if (entry_length > length - offset) break;
        const uint8_t *entry = block + offset;

        static const struct {
            const char *key;
            size_t field_offset;
        } fields[] = {
            {"TITLE", offsetof(audio_tags_t, title)},
            {"ARTIST", offsetof(audio_tags_t, artist)},
            {"ALBUM", offsetof(audio_tags_t, album)},
        };
        for (size_t field = 0U; field < sizeof(fields) / sizeof(fields[0]); ++field) {
            if (!key_matches(entry, entry_length, fields[field].key)) continue;
            const size_t key_length = strlen(fields[field].key) + 1U;
            audio_tags_text_to_utf8(AUDIO_TAGS_ENCODING_UTF8, entry + key_length,
                                    entry_length - key_length,
                                    (char *)tags + fields[field].field_offset,
                                    AUDIO_TAGS_TEXT_MAX_LEN);
            found_any = true;
            break;
        }
        offset += entry_length;
    }
    return found_any;
}

static audio_tags_picture_format_t format_for(const uint8_t *mime, size_t length)
{
    // FLAC spells the MIME type out in full, so an exact match is enough and
    // there is no three-letter shorthand to allow for.
    if (length == 10U && memcmp(mime, "image/jpeg", 10U) == 0) return AUDIO_TAGS_PICTURE_JPEG;
    if (length == 9U && memcmp(mime, "image/png", 9U) == 0) return AUDIO_TAGS_PICTURE_PNG;
    return AUDIO_TAGS_PICTURE_UNSUPPORTED;
}

bool flac_picture_describe(const uint8_t *block, size_t length,
                           audio_tags_picture_format_t *format, uint8_t *picture_type,
                           size_t *data_offset, size_t *data_length)
{
    if (block == NULL || format == NULL || picture_type == NULL || data_offset == NULL ||
        data_length == NULL) {
        return false;
    }
    if (length < 8U) return false;
    const uint32_t type = read_be32(block);
    if (type > 0xFFU) return false;
    *picture_type = (uint8_t)type;

    size_t offset = 4U;
    const size_t mime_length = read_be32(block + offset);
    offset += 4U;
    if (mime_length > length - offset) return false;
    *format = format_for(block + offset, mime_length);
    offset += mime_length;

    if (offset + 4U > length) return false;
    const size_t description_length = read_be32(block + offset);
    offset += 4U;
    if (description_length > length - offset) return false;
    offset += description_length;

    // Width, height, colour depth and palette size, none of which matter here:
    // the decoder reads the real dimensions out of the image itself.
    if (offset + 20U > length) return false;
    offset += 16U;
    *data_length = read_be32(block + offset);
    offset += 4U;
    *data_offset = offset;
    return *data_length != 0U;
}
