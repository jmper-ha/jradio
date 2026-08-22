#include "audio_tags_reader.h"

#include <string.h>

#include "flac_tags.h"
#include "id3v2.h"

/* Enough for a PICTURE block's fixed part plus a MIME type and a description
 * of any sane length. The image itself is seeked to, never read through this. */
#define FLAC_PICTURE_HEADER_MAX 512U
// A FLAC file with more metadata blocks than this is corrupt, and walking a
// corrupt chain forever is worse than missing a cover.
#define FLAC_BLOCK_LIMIT 64U

static bool read_at(FILE *file, long offset, uint8_t *destination, size_t length,
                    size_t *received)
{
    if (fseek(file, offset, SEEK_SET) != 0) return false;
    *received = fread(destination, 1U, length, file);
    return true;
}

static bool read_id3v2(FILE *file, uint8_t *scratch, size_t capacity, audio_tags_t *tags)
{
    uint8_t header[ID3V2_HEADER_SIZE];
    size_t received = 0U;
    if (!read_at(file, 0, header, sizeof(header), &received)) return false;
    if (received < sizeof(header)) return false;

    id3v2_header_t parsed;
    if (!id3v2_header_parse(header, sizeof(header), &parsed)) return false;

    size_t want = parsed.body_size;
    if (want > capacity) want = capacity;
    if (!read_at(file, (long)ID3V2_HEADER_SIZE, scratch, want, &received)) return false;
    // A short read is a truncated file, not a reason to give up: the text
    // frames sit at the front and are almost certainly all there.
    if (parsed.unsynchronised) received = id3v2_deunsynchronise(scratch, received);
    return id3v2_parse_body(&parsed, scratch, received, tags);
}

static bool read_id3v1(FILE *file, audio_tags_t *tags)
{
    if (fseek(file, -(long)ID3V1_BLOCK_SIZE, SEEK_END) != 0) return false;
    uint8_t block[ID3V1_BLOCK_SIZE];
    if (fread(block, 1U, sizeof(block), file) != sizeof(block)) return false;
    return id3v1_parse(block, sizeof(block), tags);
}

static bool read_flac(FILE *file, uint8_t *scratch, size_t capacity, audio_tags_t *tags)
{
    uint8_t signature[FLAC_SIGNATURE_SIZE];
    size_t received = 0U;
    if (!read_at(file, 0, signature, sizeof(signature), &received)) return false;
    if (received < sizeof(signature) || !flac_signature_matches(signature, received)) {
        return false;
    }

    long offset = (long)FLAC_SIGNATURE_SIZE;
    long picture_offset = 0;
    size_t picture_length = 0U;
    audio_tags_picture_format_t picture_format = AUDIO_TAGS_PICTURE_NONE;
    bool have_front_cover = false;
    bool found_any = false;

    for (unsigned int block = 0U; block < FLAC_BLOCK_LIMIT; ++block) {
        uint8_t header[FLAC_BLOCK_HEADER_SIZE];
        if (!read_at(file, offset, header, sizeof(header), &received)) break;
        if (received < sizeof(header)) break;

        bool last = false;
        uint8_t type = 0U;
        size_t length = 0U;
        if (!flac_block_header_parse(header, received, &last, &type, &length)) break;
        const long body = offset + (long)FLAC_BLOCK_HEADER_SIZE;

        if (type == FLAC_BLOCK_VORBIS_COMMENT && length <= capacity) {
            if (read_at(file, body, scratch, length, &received) &&
                flac_vorbis_comment_parse(scratch, received, tags)) {
                found_any = true;
            }
        } else if (type == FLAC_BLOCK_PICTURE) {
            const size_t want = length < FLAC_PICTURE_HEADER_MAX ? length
                                                                 : FLAC_PICTURE_HEADER_MAX;
            uint8_t head[FLAC_PICTURE_HEADER_MAX];
            audio_tags_picture_format_t format = AUDIO_TAGS_PICTURE_NONE;
            uint8_t picture_type = 0U;
            size_t data_offset = 0U;
            size_t data_length = 0U;
            if (read_at(file, body, head, want, &received) &&
                flac_picture_describe(head, received, &format, &picture_type, &data_offset,
                                      &data_length)) {
                /* The image is not read here. VORBIS_COMMENT may still be
                 * ahead of us and needs the same scratch, so the picture is
                 * fetched once the walk is over. */
                const bool is_front = picture_type == FLAC_PICTURE_TYPE_FRONT_COVER;
                if (!have_front_cover && (picture_length == 0U || is_front)) {
                    picture_offset = body + (long)data_offset;
                    picture_length = data_length;
                    picture_format = format;
                    have_front_cover = is_front;
                }
            }
        }

        if (last) break;
        offset = body + (long)length;
    }

    if (picture_length > 0U) {
        /* Reported whether or not it fits: a picture too large for this
         * buffer is still a picture, and the caller can read it where it
         * lies. Only the copy into the scratch is conditional. */
        tags->picture_format = picture_format;
        tags->picture_file_offset = (size_t)picture_offset;
        tags->picture_file_length = picture_length;
        found_any = true;
        if (picture_length <= capacity &&
            read_at(file, picture_offset, scratch, picture_length, &received) &&
            received == picture_length) {
            tags->picture_offset = 0U;
            tags->picture_length = picture_length;
        }
    }
    return found_any;
}

bool audio_tags_read_file(FILE *file, uint8_t *scratch, size_t capacity, audio_tags_t *tags)
{
    if (tags == NULL) return false;
    audio_tags_clear(tags);
    if (file == NULL || scratch == NULL || capacity == 0U) return false;

    bool found = read_id3v2(file, scratch, capacity, tags);
    if (!found) found = read_flac(file, scratch, capacity, tags);
    // Only worth reading when nothing better was found: ID3v1 holds 30
    // characters per field in an encoding it does not name.
    if (!audio_tags_have_text(tags)) found = read_id3v1(file, tags) || found;
    return found;
}
