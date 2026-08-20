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
