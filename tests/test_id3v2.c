#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fixtures/id3v2_sample.h"
#include "id3v2.h"

typedef struct {
    uint8_t data[8192];
    size_t length;
} tag_builder_t;

static void put(tag_builder_t *builder, const void *bytes, size_t length)
{
    assert(builder->length + length <= sizeof(builder->data));
    memcpy(builder->data + builder->length, bytes, length);
    builder->length += length;
}

static void put_u8(tag_builder_t *builder, uint8_t value)
{
    put(builder, &value, 1U);
}

static void put_be32(tag_builder_t *builder, uint32_t value)
{
    const uint8_t bytes[4] = {(uint8_t)(value >> 24), (uint8_t)(value >> 16),
                              (uint8_t)(value >> 8), (uint8_t)value};
    put(builder, bytes, sizeof(bytes));
}

static void put_syncsafe(tag_builder_t *builder, uint32_t value)
{
    const uint8_t bytes[4] = {(uint8_t)((value >> 21) & 0x7FU), (uint8_t)((value >> 14) & 0x7FU),
                              (uint8_t)((value >> 7) & 0x7FU), (uint8_t)(value & 0x7FU)};
    put(builder, bytes, sizeof(bytes));
}

// One Latin-1 text frame, the smallest thing the walker has to get right.
static void put_text_frame(tag_builder_t *builder, const char *id, const char *text,
                           bool syncsafe_size)
{
    const size_t body = strlen(text) + 1U;
    put(builder, id, 4U);
    if (syncsafe_size) {
        put_syncsafe(builder, (uint32_t)body);
    } else {
        put_be32(builder, (uint32_t)body);
    }
    put_u8(builder, 0U);
    put_u8(builder, 0U);
    put_u8(builder, 0U);
    put(builder, text, strlen(text));
}

static void put_picture_frame(tag_builder_t *builder, const char *mime, uint8_t picture_type,
                              const char *image, bool syncsafe_size)
{
    const size_t body = 1U + strlen(mime) + 1U + 1U + 1U + strlen(image);
    put(builder, "APIC", 4U);
    if (syncsafe_size) {
        put_syncsafe(builder, (uint32_t)body);
    } else {
        put_be32(builder, (uint32_t)body);
    }
    put_u8(builder, 0U);
    put_u8(builder, 0U);
    put_u8(builder, 0U);
    put(builder, mime, strlen(mime) + 1U);
    put_u8(builder, picture_type);
    put_u8(builder, 0U);
    put(builder, image, strlen(image));
}

static void parse(tag_builder_t *builder, uint8_t major, uint8_t flags, audio_tags_t *tags)
{
    id3v2_header_t header = {
        .major_version = major,
        .unsynchronised = (flags & 0x80U) != 0U,
        .has_extended_header = (flags & 0x40U) != 0U,
        .has_footer = false,
        .body_size = builder->length,
    };
    audio_tags_clear(tags);
    (void)id3v2_parse_body(&header, builder->data, builder->length, tags);
}

static void test_the_real_album_reads_back(void)
{
    /* The bytes of "01-1 Балтийское море.mp3" as the file has them: ID3v2.3
     * with every text frame in UTF-16. Anything the parser gets wrong about
     * real-world writers shows up here rather than on the device. */
    id3v2_header_t header;
    assert(id3v2_header_parse(id3v2_sample_tag, ID3V2_SAMPLE_TAG_LENGTH, &header));
    assert(header.major_version == 3U);
    assert(!header.unsynchronised);
    assert(!header.has_extended_header);
    assert(header.body_size == 68325U);

    audio_tags_t tags;
    audio_tags_clear(&tags);
    assert(id3v2_parse_body(&header, id3v2_sample_tag + ID3V2_HEADER_SIZE,
                            ID3V2_SAMPLE_TAG_LENGTH - ID3V2_HEADER_SIZE, &tags));
    assert(strcmp(tags.title, "01-1 Балтийское море") == 0);
    assert(strcmp(tags.artist, "Сахарнов С., Орлов О., Бирман Н.") == 0);
    assert(strcmp(tags.album,
                  "Циклы \"Вместе с нами по морям\" и \"Встреча в кают-компании\"") == 0);
}

static void test_a_cover_cut_off_by_the_buffer_is_not_reported(void)
{
    /* The same bytes: the tag declares 68325, the fixture holds 1024, and the
     * APIC frame is the one that straddles the end. Reporting the 220 bytes
     * that did arrive would send a JPEG decoder into a file that stops in the
     * middle of a scan. */
    id3v2_header_t header;
    assert(id3v2_header_parse(id3v2_sample_tag, ID3V2_SAMPLE_TAG_LENGTH, &header));
    audio_tags_t tags;
    audio_tags_clear(&tags);
    (void)id3v2_parse_body(&header, id3v2_sample_tag + ID3V2_HEADER_SIZE,
                           ID3V2_SAMPLE_TAG_LENGTH - ID3V2_HEADER_SIZE, &tags);
    assert(tags.picture_length == 0U);
    assert(tags.picture_format == AUDIO_TAGS_PICTURE_NONE);
}

static void test_header_rejects_what_is_not_a_tag(void)
{
    id3v2_header_t header;
    const uint8_t not_a_tag[ID3V2_HEADER_SIZE] = {'I', 'D', '4', 3U, 0U, 0U, 0U, 0U, 0U, 0U};
    assert(!id3v2_header_parse(not_a_tag, sizeof(not_a_tag), &header));

    const uint8_t future[ID3V2_HEADER_SIZE] = {'I', 'D', '3', 5U, 0U, 0U, 0U, 0U, 0U, 0U};
    assert(!id3v2_header_parse(future, sizeof(future), &header));

    /* The size is syncsafe by definition, so a high bit there means these ten
     * bytes are audio that happens to start with the letters ID3. */
    const uint8_t bad_size[ID3V2_HEADER_SIZE] = {'I', 'D', '3', 3U, 0U, 0U, 0x80U, 0U, 0U, 0U};
    assert(!id3v2_header_parse(bad_size, sizeof(bad_size), &header));
}

static void test_text_and_a_complete_picture(void)
{
    tag_builder_t builder = {.length = 0U};
    put_text_frame(&builder, "TIT2", "Title", false);
    put_text_frame(&builder, "TPE1", "Artist", false);
    put_text_frame(&builder, "TALB", "Album", false);
    put_picture_frame(&builder, "image/jpeg", 3U, "JPEGBYTES", false);

    audio_tags_t tags;
    parse(&builder, 3U, 0U, &tags);
    assert(strcmp(tags.title, "Title") == 0);
    assert(strcmp(tags.artist, "Artist") == 0);
    assert(strcmp(tags.album, "Album") == 0);
    assert(tags.picture_format == AUDIO_TAGS_PICTURE_JPEG);
    assert(tags.picture_length == strlen("JPEGBYTES"));
    assert(memcmp(builder.data + tags.picture_offset, "JPEGBYTES", tags.picture_length) == 0);
}

static void test_the_front_cover_wins_over_whatever_came_first(void)
{
    /* Files carry a band photo, a back cover and a file icon alongside the
     * front one. Taking the first picture puts the drummer on the screen. */
    tag_builder_t builder = {.length = 0U};
    put_picture_frame(&builder, "image/jpeg", 8U, "BANDPHOTO", false);
    put_picture_frame(&builder, "image/jpeg", 3U, "FRONT", false);
    put_picture_frame(&builder, "image/jpeg", 4U, "BACKCOVER", false);

    audio_tags_t tags;
    parse(&builder, 3U, 0U, &tags);
    assert(tags.picture_length == strlen("FRONT"));
    assert(memcmp(builder.data + tags.picture_offset, "FRONT", tags.picture_length) == 0);
}

static void test_a_picture_type_nothing_can_draw_is_still_a_picture(void)
{
    tag_builder_t builder = {.length = 0U};
    put_picture_frame(&builder, "image/bmp", 3U, "BMPBYTES", false);
    audio_tags_t tags;
    parse(&builder, 3U, 0U, &tags);
    // Kept apart from "no cover" so the caller can tell the two apart.
    assert(tags.picture_format == AUDIO_TAGS_PICTURE_UNSUPPORTED);
    assert(tags.picture_length > 0U);
}

static void test_version_four_sizes_are_syncsafe(void)
{
    tag_builder_t builder = {.length = 0U};
    put_text_frame(&builder, "TIT2", "Syncsafe", true);
    audio_tags_t tags;
    parse(&builder, 4U, 0U, &tags);
    assert(strcmp(tags.title, "Syncsafe") == 0);
}

static void test_a_version_four_tag_with_version_three_sizes_still_reads(void)
{
    /* Writers that were built for v2.3 and had their version byte bumped keep
     * writing plain 32-bit sizes. The two readings agree only while every byte
     * stays below 0x80, which a 200-byte frame already breaks. */
    tag_builder_t builder = {.length = 0U};
    char text[200];
    memset(text, 'x', sizeof(text) - 1U);
    text[sizeof(text) - 1U] = '\0';
    put_text_frame(&builder, "TIT2", text, false);
    put_text_frame(&builder, "TPE1", "After", false);

    audio_tags_t tags;
    parse(&builder, 4U, 0U, &tags);
    // The field stops at its own limit, which is shorter than this frame.
    assert(strlen(tags.title) == AUDIO_TAGS_TEXT_MAX_LEN - 1U);
    assert(strspn(tags.title, "x") == strlen(tags.title));
    // The frame after it is the proof: a wrong size lands the walk in the
    // middle of this one.
    assert(strcmp(tags.artist, "After") == 0);
}

static void test_unsynchronisation_is_undone_before_the_walk(void)
{
    /* With the inserted zeroes still in place every frame size past the first
     * 0xFF is wrong, so this has to happen to the whole body at once. */
    uint8_t body[] = {0xFFU, 0x00U, 0x0AU, 0xFFU, 0x00U, 0xFFU, 0x00U, 0x01U};
    const size_t length = id3v2_deunsynchronise(body, sizeof(body));
    const uint8_t expected[] = {0xFFU, 0x0AU, 0xFFU, 0xFFU, 0x01U};
    assert(length == sizeof(expected));
    assert(memcmp(body, expected, length) == 0);
}

static void test_padding_ends_the_walk(void)
{
    tag_builder_t builder = {.length = 0U};
    put_text_frame(&builder, "TIT2", "Title", false);
    // Real tags leave hundreds of zero bytes so an editor can grow a frame
    // without rewriting the file.
    const uint8_t padding[64] = {0};
    put(&builder, padding, sizeof(padding));

    audio_tags_t tags;
    parse(&builder, 3U, 0U, &tags);
    assert(strcmp(tags.title, "Title") == 0);
}

static void test_an_extended_header_is_stepped_over(void)
{
    tag_builder_t v3 = {.length = 0U};
    put_be32(&v3, 6U);
    const uint8_t extended[6] = {0};
    put(&v3, extended, sizeof(extended));
    put_text_frame(&v3, "TIT2", "Title", false);
    audio_tags_t tags;
    parse(&v3, 3U, 0x40U, &tags);
    assert(strcmp(tags.title, "Title") == 0);

    // v2.4 counts the four size bytes as part of the extended header, v2.3
    // does not, and getting that backwards lands the walk four bytes off.
    tag_builder_t v4 = {.length = 0U};
    put_syncsafe(&v4, 10U);
    const uint8_t extended4[6] = {0};
    put(&v4, extended4, sizeof(extended4));
    put_text_frame(&v4, "TIT2", "Four", true);
    parse(&v4, 4U, 0x40U, &tags);
    assert(strcmp(tags.title, "Four") == 0);
}

static void test_a_compressed_frame_is_skipped_not_decoded(void)
{
    tag_builder_t builder = {.length = 0U};
    put(&builder, "TIT2", 4U);
    put_be32(&builder, 6U);
    put_u8(&builder, 0U);
    put_u8(&builder, 0x80U);
    const uint8_t rubbish[6] = {0x78U, 0x9CU, 0x01U, 0x02U, 0x03U, 0x04U};
    put(&builder, rubbish, sizeof(rubbish));
    put_text_frame(&builder, "TPE1", "Artist", false);

    audio_tags_t tags;
    parse(&builder, 3U, 0U, &tags);
    // Deflated bytes read as text are noise; the frame after it is not.
    assert(tags.title[0] == '\0');
    assert(strcmp(tags.artist, "Artist") == 0);
}

static void test_a_data_length_indicator_is_not_part_of_the_text(void)
{
    tag_builder_t builder = {.length = 0U};
    put(&builder, "TIT2", 4U);
    put_syncsafe(&builder, 4U + 1U + 5U);
    put_u8(&builder, 0U);
    put_u8(&builder, 0x01U);
    put_syncsafe(&builder, 6U);
    put_u8(&builder, 0U);
    put(&builder, "Title", 5U);

    audio_tags_t tags;
    parse(&builder, 4U, 0U, &tags);
    assert(strcmp(tags.title, "Title") == 0);
}

static void test_version_two_frames_are_three_characters(void)
{
    tag_builder_t builder = {.length = 0U};
    put(&builder, "TT2", 3U);
    const uint8_t size[3] = {0U, 0U, 6U};
    put(&builder, size, sizeof(size));
    put_u8(&builder, 0U);
    put(&builder, "Title", 5U);

    put(&builder, "PIC", 3U);
    // Encoding, three format letters, the picture type, an empty description
    // and three bytes of image.
    const uint8_t picture_size[3] = {0U, 0U, 9U};
    put(&builder, picture_size, sizeof(picture_size));
    put_u8(&builder, 0U);
    put(&builder, "JPG", 3U);
    put_u8(&builder, 3U);
    put_u8(&builder, 0U);
    put(&builder, "IMG", 3U);

    audio_tags_t tags;
    parse(&builder, 2U, 0U, &tags);
    assert(strcmp(tags.title, "Title") == 0);
    assert(tags.picture_format == AUDIO_TAGS_PICTURE_JPEG);
    assert(tags.picture_length == 3U);
    assert(memcmp(builder.data + tags.picture_offset, "IMG", 3U) == 0);
}

static void test_a_utf16_picture_description_ends_on_a_zero_pair(void)
{
    /* A single zero byte appears inside every second UTF-16 character, so
     * looking for one puts the start of the image in the wrong place. */
    tag_builder_t builder = {.length = 0U};
    const uint8_t description[] = {0xFFU, 0xFEU, 0x1CU, 0x04U, 0x00U, 0x00U};
    const size_t body = 1U + strlen("image/jpeg") + 1U + 1U + sizeof(description) + 4U;
    put(&builder, "APIC", 4U);
    put_be32(&builder, (uint32_t)body);
    put_u8(&builder, 0U);
    put_u8(&builder, 0U);
    put_u8(&builder, 1U);
    put(&builder, "image/jpeg", strlen("image/jpeg") + 1U);
    put_u8(&builder, 3U);
    put(&builder, description, sizeof(description));
    put(&builder, "IMGS", 4U);

    audio_tags_t tags;
    parse(&builder, 3U, 0U, &tags);
    assert(tags.picture_length == 4U);
    assert(memcmp(builder.data + tags.picture_offset, "IMGS", 4U) == 0);
}

static void test_id3v1_is_the_last_resort(void)
{
    uint8_t block[ID3V1_BLOCK_SIZE];
    memset(block, ' ', sizeof(block));
    memcpy(block, "TAG", 3U);
    memcpy(block + 3, "Title", 5U);
    memcpy(block + 33, "Artist", 6U);
    memcpy(block + 63, "Album", 5U);

    audio_tags_t tags;
    audio_tags_clear(&tags);
    assert(id3v1_parse(block, sizeof(block), &tags));
    assert(strcmp(tags.title, "Title") == 0);
    assert(strcmp(tags.artist, "Artist") == 0);
    assert(strcmp(tags.album, "Album") == 0);

    block[0] = 'X';
    assert(!id3v1_parse(block, sizeof(block), &tags));
}

int main(void)
{
    test_the_real_album_reads_back();
    test_a_cover_cut_off_by_the_buffer_is_not_reported();
    test_header_rejects_what_is_not_a_tag();
    test_text_and_a_complete_picture();
    test_the_front_cover_wins_over_whatever_came_first();
    test_a_picture_type_nothing_can_draw_is_still_a_picture();
    test_version_four_sizes_are_syncsafe();
    test_a_version_four_tag_with_version_three_sizes_still_reads();
    test_unsynchronisation_is_undone_before_the_walk();
    test_padding_ends_the_walk();
    test_an_extended_header_is_stepped_over();
    test_a_compressed_frame_is_skipped_not_decoded();
    test_a_data_length_indicator_is_not_part_of_the_text();
    test_version_two_frames_are_three_characters();
    test_a_utf16_picture_description_ends_on_a_zero_pair();
    test_id3v1_is_the_last_resort();
    printf("id3v2 tests passed\n");
    return 0;
}
