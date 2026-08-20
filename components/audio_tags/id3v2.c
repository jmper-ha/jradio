#include "id3v2.h"

#include <string.h>

#define ID3V2_FLAG_UNSYNCHRONISED 0x80U
#define ID3V2_FLAG_EXTENDED_HEADER 0x40U
#define ID3V2_FLAG_FOOTER 0x10U

// v2.3 frame flags, second byte
#define ID3V2_3_FRAME_COMPRESSED 0x80U
#define ID3V2_3_FRAME_ENCRYPTED 0x40U
#define ID3V2_3_FRAME_GROUPED 0x20U
// v2.4 frame flags, second byte
#define ID3V2_4_FRAME_GROUPED 0x40U
#define ID3V2_4_FRAME_COMPRESSED 0x08U
#define ID3V2_4_FRAME_ENCRYPTED 0x04U
#define ID3V2_4_FRAME_UNSYNCHRONISED 0x02U
#define ID3V2_4_FRAME_DATA_LENGTH 0x01U

#define ID3V2_PICTURE_TYPE_FRONT_COVER 3U

static uint32_t read_syncsafe(const uint8_t *bytes)
{
    return ((uint32_t)(bytes[0] & 0x7FU) << 21) | ((uint32_t)(bytes[1] & 0x7FU) << 14) |
           ((uint32_t)(bytes[2] & 0x7FU) << 7) | (uint32_t)(bytes[3] & 0x7FU);
}

static uint32_t read_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}

bool id3v2_header_parse(const uint8_t *header, size_t length, id3v2_header_t *out)
{
    if (header == NULL || out == NULL || length < ID3V2_HEADER_SIZE) return false;
    if (header[0] != 'I' || header[1] != 'D' || header[2] != '3') return false;
    // 0xFF is reserved as an invalid version, and the two size fields must be
    // syncsafe - a high bit anywhere means this is not a tag header at all.
    if (header[3] < 2U || header[3] > 4U) return false;
    for (size_t index = 6U; index < ID3V2_HEADER_SIZE; ++index) {
        if ((header[index] & 0x80U) != 0U) return false;
    }

    out->major_version = header[3];
    out->unsynchronised = (header[5] & ID3V2_FLAG_UNSYNCHRONISED) != 0U;
    out->has_extended_header = (header[5] & ID3V2_FLAG_EXTENDED_HEADER) != 0U;
    // The footer bit is only defined from v2.4; earlier versions leave that
    // bit clear anyway, so no version test is needed here.
    out->has_footer = (header[5] & ID3V2_FLAG_FOOTER) != 0U;
    out->body_size = read_syncsafe(header + 6);
    return true;
}

size_t id3v2_deunsynchronise(uint8_t *body, size_t length)
{
    if (body == NULL) return 0U;
    size_t write = 0U;
    for (size_t read = 0U; read < length; ++read) {
        body[write++] = body[read];
        // The inserted byte only ever follows 0xFF, and only one of them, so
        // dropping it unconditionally cannot eat real data.
        if (body[read] == 0xFFU && read + 1U < length && body[read + 1U] == 0x00U) ++read;
    }
    return write;
}

static bool frame_id_char(uint8_t value)
{
    return (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9');
}

static bool frame_id_valid(const uint8_t *id, size_t width)
{
    for (size_t index = 0U; index < width; ++index) {
        if (!frame_id_char(id[index])) return false;
    }
    return true;
}

/* True when whatever follows a frame of the assumed size still looks like a
 * tag: another frame, padding, or the end of the buffer. */
static bool tail_is_plausible(const uint8_t *next, size_t remaining, size_t id_width)
{
    if (remaining < id_width) return true;
    if (next[0] == 0x00U) return true;
    return frame_id_valid(next, id_width);
}

/* v2.4 sizes are syncsafe. Writers that were built for v2.3 and then bumped
 * the version byte keep writing plain 32-bit sizes, and the two agree only
 * while every byte stays below 0x80 - which a cover never does. Picking the
 * reading whose next frame lands on something tag-shaped recovers those files
 * instead of stopping at the first frame over 128 bytes. */
static size_t frame_size_v4(const uint8_t *frame, size_t remaining)
{
    const size_t syncsafe = read_syncsafe(frame + 4);
    const size_t plain = read_be32(frame + 4);
    if (syncsafe == plain) return syncsafe;

    const bool syncsafe_fits = 10U + syncsafe <= remaining;
    const bool plain_fits = 10U + plain <= remaining;
    if (syncsafe_fits &&
        tail_is_plausible(frame + 10U + syncsafe, remaining - 10U - syncsafe, 4U)) {
        return syncsafe;
    }
    if (plain_fits && tail_is_plausible(frame + 10U + plain, remaining - 10U - plain, 4U)) {
        return plain;
    }
    return syncsafe;
}

static bool text_field_for(const uint8_t *id, size_t width, audio_tags_t *tags,
                           char **destination)
{
    static const struct {
        const char *id;
        size_t offset;
    } fields[] = {
        {"TIT2", offsetof(audio_tags_t, title)},  {"TPE1", offsetof(audio_tags_t, artist)},
        {"TALB", offsetof(audio_tags_t, album)},  {"TT2", offsetof(audio_tags_t, title)},
        {"TP1", offsetof(audio_tags_t, artist)},  {"TAL", offsetof(audio_tags_t, album)},
    };
    for (size_t index = 0U; index < sizeof(fields) / sizeof(fields[0]); ++index) {
        if (strlen(fields[index].id) != width) continue;
        if (memcmp(id, fields[index].id, width) != 0) continue;
        *destination = (char *)tags + fields[index].offset;
        return true;
    }
    return false;
}

static bool contains_ignoring_case(const uint8_t *haystack, size_t length, const char *needle)
{
    const size_t needle_length = strlen(needle);
    if (needle_length > length) return false;
    for (size_t start = 0U; start + needle_length <= length; ++start) {
        size_t index = 0U;
        while (index < needle_length) {
            uint8_t value = haystack[start + index];
            if (value >= 'A' && value <= 'Z') value = (uint8_t)(value - 'A' + 'a');
            if (value != (uint8_t)needle[index]) break;
            ++index;
        }
        if (index == needle_length) return true;
    }
    return false;
}

static audio_tags_picture_format_t picture_format_for(const uint8_t *mime, size_t length)
{
    if (contains_ignoring_case(mime, length, "jpeg") ||
        contains_ignoring_case(mime, length, "jpg")) {
        return AUDIO_TAGS_PICTURE_JPEG;
    }
    if (contains_ignoring_case(mime, length, "png")) return AUDIO_TAGS_PICTURE_PNG;
    return AUDIO_TAGS_PICTURE_UNSUPPORTED;
}

/* Steps over the description, which is terminated by one zero byte or - when
 * the frame declared a 16-bit encoding - by a zero *pair* on an even offset
 * from the description's own start. */
static bool skip_description(const uint8_t *body, size_t length, size_t start, uint8_t encoding,
                             size_t *after)
{
    const bool wide = encoding == AUDIO_TAGS_ENCODING_UTF16_BOM ||
                      encoding == AUDIO_TAGS_ENCODING_UTF16_BE;
    size_t index = start;
    if (wide) {
        while (index + 1U < length) {
            if (body[index] == 0U && body[index + 1U] == 0U) {
                *after = index + 2U;
                return true;
            }
            index += 2U;
        }
        return false;
    }
    while (index < length) {
        if (body[index] == 0U) {
            *after = index + 1U;
            return true;
        }
        ++index;
    }
    return false;
}

/* Records a picture unless a better one is already in hand. Files carry
 * several - a back cover, an artist photo, a file icon - and only the front
 * cover belongs on the player screen. */
static void note_picture(audio_tags_t *tags, audio_tags_picture_format_t format,
                         uint8_t picture_type, size_t offset, size_t length,
                         bool *have_front_cover)
{
    if (length == 0U) return;
    const bool is_front = picture_type == ID3V2_PICTURE_TYPE_FRONT_COVER;
    if (*have_front_cover && !is_front) return;
    if (tags->picture_length != 0U && !is_front) return;

    tags->picture_format = format;
    tags->picture_offset = offset;
    tags->picture_length = length;
    if (is_front) *have_front_cover = true;
}

static void parse_picture_frame(const uint8_t *body, size_t length, size_t frame_offset,
                                bool short_format, audio_tags_t *tags, bool *have_front_cover)
{
    if (length < 4U) return;
    const uint8_t encoding = body[0];
    size_t cursor;
    audio_tags_picture_format_t format;

    if (short_format) {
        // v2.2 names the format with three letters instead of a MIME type.
        format = picture_format_for(body + 1, 3U);
        cursor = 4U;
    } else {
        size_t mime_end = 1U;
        while (mime_end < length && body[mime_end] != 0U) ++mime_end;
        if (mime_end >= length) return;
        format = picture_format_for(body + 1, mime_end - 1U);
        cursor = mime_end + 2U;
    }
    if (cursor > length) return;
    const uint8_t picture_type = body[cursor - 1U];

    size_t data_start;
    if (!skip_description(body, length, cursor, encoding, &data_start)) return;
    if (data_start >= length) return;
    note_picture(tags, format, picture_type, frame_offset + data_start, length - data_start,
                 have_front_cover);
}

bool id3v2_parse_body(const id3v2_header_t *header, const uint8_t *body, size_t length,
                      audio_tags_t *tags)
{
    if (header == NULL || body == NULL || tags == NULL) return false;

    const bool short_frames = header->major_version == 2U;
    const size_t id_width = short_frames ? 3U : 4U;
    const size_t frame_header_size = short_frames ? 6U : 10U;

    size_t offset = 0U;
    if (header->has_extended_header && !short_frames) {
        if (length < 4U) return false;
        /* v2.4 counts the extended header's own four size bytes, v2.3 does
         * not - and v2.3 writes the size plain rather than syncsafe. */
        const size_t extended =
            header->major_version >= 4U ? read_syncsafe(body) : read_be32(body) + 4U;
        if (extended >= length) return false;
        offset = extended;
    }

    bool have_front_cover = false;
    bool found_any = false;
    while (offset + frame_header_size <= length) {
        const uint8_t *frame = body + offset;
        // A zero where a frame identifier belongs is the padding that fills
        // the rest of the tag, not a malformed frame.
        if (frame[0] == 0x00U) break;
        if (!frame_id_valid(frame, id_width)) break;

        size_t frame_size;
        uint8_t frame_flags = 0U;
        if (short_frames) {
            frame_size = ((size_t)frame[3] << 16) | ((size_t)frame[4] << 8) | (size_t)frame[5];
        } else if (header->major_version >= 4U) {
            frame_size = frame_size_v4(frame, length - offset);
            frame_flags = frame[9];
        } else {
            frame_size = read_be32(frame + 4);
            frame_flags = frame[9];
        }

        size_t body_offset = offset + frame_header_size;
        size_t body_length = frame_size;
        /* A frame that runs past the end of what was read is not a short
         * frame, it is a cut one, and there is nothing after it either. The
         * cover of a tag too big for the read buffer arrives exactly this way:
         * handing its first few hundred bytes to a JPEG decoder as if they
         * were the whole image is worse than showing no cover at all. */
        if (body_offset >= length || body_length > length - body_offset) break;

        bool usable = true;
        if (header->major_version >= 4U) {
            if ((frame_flags & (ID3V2_4_FRAME_COMPRESSED | ID3V2_4_FRAME_ENCRYPTED |
                                ID3V2_4_FRAME_UNSYNCHRONISED)) != 0U) {
                usable = false;
            }
            if (usable && (frame_flags & ID3V2_4_FRAME_GROUPED) != 0U && body_length >= 1U) {
                ++body_offset;
                --body_length;
            }
            if (usable && (frame_flags & ID3V2_4_FRAME_DATA_LENGTH) != 0U && body_length >= 4U) {
                body_offset += 4U;
                body_length -= 4U;
            }
        } else if (!short_frames) {
            if ((frame_flags & (ID3V2_3_FRAME_COMPRESSED | ID3V2_3_FRAME_ENCRYPTED)) != 0U) {
                usable = false;
            }
            if (usable && (frame_flags & ID3V2_3_FRAME_GROUPED) != 0U && body_length >= 1U) {
                ++body_offset;
                --body_length;
            }
        }

        char *destination = NULL;
        if (!usable) {
            // nothing to do; fall through to the next frame
        } else if (text_field_for(frame, id_width, tags, &destination)) {
            if (body_length >= 2U) {
                audio_tags_text_to_utf8(body[body_offset], body + body_offset + 1U,
                                        body_length - 1U, destination,
                                        AUDIO_TAGS_TEXT_MAX_LEN);
                found_any = true;
            }
        } else if ((short_frames && memcmp(frame, "PIC", 3U) == 0) ||
                   (!short_frames && memcmp(frame, "APIC", 4U) == 0)) {
            parse_picture_frame(body + body_offset, body_length, body_offset, short_frames,
                                tags, &have_front_cover);
            found_any = found_any || tags->picture_length != 0U;
        }

        offset += frame_header_size + frame_size;
    }
    return found_any;
}

static void copy_id3v1_field(const uint8_t *field, char *destination)
{
    size_t length = 30U;
    while (length > 0U && (field[length - 1U] == ' ' || field[length - 1U] == '\0')) --length;
    audio_tags_text_to_utf8(AUDIO_TAGS_ENCODING_ISO8859_1, field, length, destination,
                            AUDIO_TAGS_TEXT_MAX_LEN);
}

bool id3v1_parse(const uint8_t *block, size_t length, audio_tags_t *tags)
{
    if (block == NULL || tags == NULL || length < ID3V1_BLOCK_SIZE) return false;
    if (block[0] != 'T' || block[1] != 'A' || block[2] != 'G') return false;
    copy_id3v1_field(block + 3, tags->title);
    copy_id3v1_field(block + 33, tags->artist);
    copy_id3v1_field(block + 63, tags->album);
    return true;
}
