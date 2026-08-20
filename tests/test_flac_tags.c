#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "flac_tags.h"

typedef struct {
    uint8_t data[1024];
    size_t length;
} block_builder_t;

static void put(block_builder_t *builder, const void *bytes, size_t length)
{
    assert(builder->length + length <= sizeof(builder->data));
    memcpy(builder->data + builder->length, bytes, length);
    builder->length += length;
}

static void put_le32(block_builder_t *builder, uint32_t value)
{
    const uint8_t bytes[4] = {(uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16),
                              (uint8_t)(value >> 24)};
    put(builder, bytes, sizeof(bytes));
}

static void put_be32(block_builder_t *builder, uint32_t value)
{
    const uint8_t bytes[4] = {(uint8_t)(value >> 24), (uint8_t)(value >> 16),
                              (uint8_t)(value >> 8), (uint8_t)value};
    put(builder, bytes, sizeof(bytes));
}

static void put_comment(block_builder_t *builder, const char *entry)
{
    put_le32(builder, (uint32_t)strlen(entry));
    put(builder, entry, strlen(entry));
}

static void test_the_block_header_splits_into_three_parts(void)
{
    const uint8_t header[4] = {0x84U, 0x00U, 0x12U, 0x34U};
    bool last = false;
    uint8_t type = 0U;
    size_t length = 0U;
    assert(flac_block_header_parse(header, sizeof(header), &last, &type, &length));
    assert(last);
    assert(type == FLAC_BLOCK_VORBIS_COMMENT);
    assert(length == 0x1234U);

    // 127 is reserved as invalid, and a walk that trusts it seeks into the
    // audio frames and reads noise as block headers from then on.
    const uint8_t invalid[4] = {0x7FU, 0U, 0U, 0U};
    assert(!flac_block_header_parse(invalid, sizeof(invalid), &last, &type, &length));
}

static void test_vorbis_comments_are_named_fields(void)
{
    block_builder_t builder = {.length = 0U};
    const char *vendor = "reference libFLAC";
    put_le32(&builder, (uint32_t)strlen(vendor));
    put(&builder, vendor, strlen(vendor));
    put_le32(&builder, 4U);
    // The keys are case-insensitive by specification, and writers disagree
    // about which case to use.
    put_comment(&builder, "title=Балтийское море");
    put_comment(&builder, "ARTIST=Сахарнов С.");
    put_comment(&builder, "Album=Вместе с нами по морям");
    put_comment(&builder, "TRACKNUMBER=1");

    audio_tags_t tags;
    audio_tags_clear(&tags);
    assert(flac_vorbis_comment_parse(builder.data, builder.length, &tags));
    assert(strcmp(tags.title, "Балтийское море") == 0);
    assert(strcmp(tags.artist, "Сахарнов С.") == 0);
    assert(strcmp(tags.album, "Вместе с нами по морям") == 0);
}

static void test_a_truncated_comment_block_stops_rather_than_reads_on(void)
{
    block_builder_t builder = {.length = 0U};
    put_le32(&builder, 0U);
    put_le32(&builder, 2U);
    put_comment(&builder, "TITLE=Here");
    // Announces an entry that the block does not contain.
    put_le32(&builder, 4096U);

    audio_tags_t tags;
    audio_tags_clear(&tags);
    assert(flac_vorbis_comment_parse(builder.data, builder.length, &tags));
    assert(strcmp(tags.title, "Here") == 0);
}

static void test_a_picture_block_is_described_without_its_image(void)
{
    /* The header is a few dozen bytes and the image is hundreds of kilobytes,
     * so the walker reads only the front of the block and seeks to the rest. */
    block_builder_t builder = {.length = 0U};
    put_be32(&builder, 3U);
    put_be32(&builder, (uint32_t)strlen("image/jpeg"));
    put(&builder, "image/jpeg", strlen("image/jpeg"));
    put_be32(&builder, (uint32_t)strlen("cover"));
    put(&builder, "cover", strlen("cover"));
    put_be32(&builder, 250U);
    put_be32(&builder, 250U);
    put_be32(&builder, 24U);
    put_be32(&builder, 0U);
    put_be32(&builder, 59380U);

    audio_tags_picture_format_t format = AUDIO_TAGS_PICTURE_NONE;
    uint8_t picture_type = 0U;
    size_t data_offset = 0U;
    size_t data_length = 0U;
    assert(flac_picture_describe(builder.data, builder.length, &format, &picture_type,
                                 &data_offset, &data_length));
    assert(format == AUDIO_TAGS_PICTURE_JPEG);
    assert(picture_type == FLAC_PICTURE_TYPE_FRONT_COVER);
    assert(data_offset == builder.length);
    assert(data_length == 59380U);
}

static void test_a_picture_header_that_runs_past_the_block_is_refused(void)
{
    block_builder_t builder = {.length = 0U};
    put_be32(&builder, 3U);
    // A MIME length longer than everything that follows it.
    put_be32(&builder, 4096U);
    put(&builder, "image/jpeg", strlen("image/jpeg"));

    audio_tags_picture_format_t format = AUDIO_TAGS_PICTURE_NONE;
    uint8_t picture_type = 0U;
    size_t data_offset = 0U;
    size_t data_length = 0U;
    assert(!flac_picture_describe(builder.data, builder.length, &format, &picture_type,
                                  &data_offset, &data_length));
}

static void test_the_signature_is_four_bytes(void)
{
    assert(flac_signature_matches((const uint8_t *)"fLaCxx", 6U));
    assert(!flac_signature_matches((const uint8_t *)"FLAC", 4U));
    assert(!flac_signature_matches((const uint8_t *)"fLa", 3U));
}

int main(void)
{
    test_the_block_header_splits_into_three_parts();
    test_vorbis_comments_are_named_fields();
    test_a_truncated_comment_block_stops_rather_than_reads_on();
    test_a_picture_block_is_described_without_its_image();
    test_a_picture_header_that_runs_past_the_block_is_refused();
    test_the_signature_is_four_bytes();
    printf("flac_tags tests passed\n");
    return 0;
}
