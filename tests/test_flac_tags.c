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

/* The first 34 bytes after the block header of "01 - No Face, No Name.flac",
 * kept verbatim: 44.1 kHz, stereo, 16-bit, 16415196 samples. The numbers a
 * FLAC track's progress bar is drawn from, out of a real file rather than
 * assembled from the specification. */
static const uint8_t streaminfo_sample[FLAC_STREAMINFO_SIZE] = {
    0x10, 0x00, 0x10, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x32, 0xDE, 0x0A, 0xC4,
    0x42, 0xF0, 0x00, 0xFA, 0x79, 0xDC, 0x5F, 0x80, 0xE7, 0xD0, 0x7C, 0xF8,
    0xA8, 0xA1, 0xA0, 0x24, 0xD5, 0xAE, 0x2F, 0xB3, 0xA1, 0x6B,
};

static void test_streaminfo_reads_the_numbers_a_track_is_measured_by(void)
{
    flac_streaminfo_t info;
    assert(flac_streaminfo_parse(streaminfo_sample, sizeof(streaminfo_sample), &info));
    assert(info.sample_rate_hz == 44100U);
    assert(info.channels == 2U);
    assert(info.bits_per_sample == 16U);
    // 6:12. Nothing else in the file states this.
    assert(info.total_samples == 16415196U);
}

static void test_streaminfo_fields_are_not_on_byte_boundaries(void)
{
    /* The rate is 20 bits, the channel count 3 and the depth 5, and the
     * sample count straddles five bytes - so a reader that took whole bytes
     * would get plausible-looking nonsense rather than an obvious failure.
     * 96 kHz, 8 channels, 24-bit, 2^36-1 samples exercises the top of each. */
    uint8_t block[FLAC_STREAMINFO_SIZE] = {0};
    // 96000 is 0x17700, and those 20 bits end four bits into byte 12.
    block[10] = 0x17;
    block[11] = 0x70;
    // Rate's low nibble 0, then channels - 1 = 7, then the top bit of 24 - 1.
    block[12] = 0x00 | (7U << 1) | 0x01;
    // The remaining four bits of the depth, then the sample count's top nibble.
    block[13] = 0x7F;
    block[14] = 0xFF;
    block[15] = 0xFF;
    block[16] = 0xFF;
    block[17] = 0xFF;

    flac_streaminfo_t info;
    assert(flac_streaminfo_parse(block, sizeof(block), &info));
    assert(info.sample_rate_hz == 96000U);
    assert(info.channels == 8U);
    assert(info.bits_per_sample == 24U);
    assert(info.total_samples == 0xFFFFFFFFFU);
}

/* The first frame header of "01 - No Face, No Name.flac", at byte 9199, with
 * the bytes that follow it - a real one, because the CRC-8 at its end is what
 * these tests are about and no invented header would have a right one. */
static const uint8_t frame_header_sample[] = {
    0xFF, 0xF8, 0xC9, 0x18, 0x00, 0xC2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static void test_a_frame_header_is_read_with_its_crc(void)
{
    flac_frame_header_t header;
    assert(flac_frame_header_parse(frame_header_sample, sizeof(frame_header_sample), &header));
    assert(header.block_size == 4096U);
    assert(header.sample_rate_hz == 44100U);
    assert(header.channels == 2U);
    assert(header.header_bytes == 6U);
}

static void test_a_pattern_that_only_looks_like_a_header_is_refused(void)
{
    /* Why the CRC is checked at all: 0xFF 0xF8 turns up inside compressed
     * audio about once in every ten megabytes with everything else about it
     * plausible, and a frame started there feeds the decoder rubbish. */
    uint8_t spoiled[sizeof(frame_header_sample)];
    memcpy(spoiled, frame_header_sample, sizeof(spoiled));
    spoiled[5] ^= 0x01U;  // the CRC byte itself
    flac_frame_header_t header;
    assert(!flac_frame_header_parse(spoiled, sizeof(spoiled), &header));

    memcpy(spoiled, frame_header_sample, sizeof(spoiled));
    spoiled[4] ^= 0x40U;  // the frame number, which the CRC covers
    assert(!flac_frame_header_parse(spoiled, sizeof(spoiled), &header));

    // Reserved values, each of which marks something that is not a header.
    memcpy(spoiled, frame_header_sample, sizeof(spoiled));
    spoiled[1] = 0xFAU;  // reserved bit set beside the sync
    assert(!flac_frame_header_parse(spoiled, sizeof(spoiled), &header));
    memcpy(spoiled, frame_header_sample, sizeof(spoiled));
    spoiled[2] &= 0x0FU;  // block size code 0
    assert(!flac_frame_header_parse(spoiled, sizeof(spoiled), &header));
    memcpy(spoiled, frame_header_sample, sizeof(spoiled));
    spoiled[3] |= 0x01U;  // reserved bit after the depth
    assert(!flac_frame_header_parse(spoiled, sizeof(spoiled), &header));
}

static void test_the_sync_is_found_where_a_jump_lands(void)
{
    /* What a jump does: read a window from wherever the estimate landed, and
     * start at the first frame in it. */
    uint8_t window[64];
    memset(window, 0x5A, sizeof(window));
    memcpy(window + 17U, frame_header_sample, sizeof(frame_header_sample));

    flac_streaminfo_t info;
    assert(flac_streaminfo_parse(streaminfo_sample, sizeof(streaminfo_sample), &info));
    size_t offset = 0U;
    assert(flac_frame_find_sync(window, sizeof(window), &info, &offset));
    assert(offset == 17U);

    // Nothing to find is not a failure of the search, and must not be reported
    // as an offset of zero.
    memset(window, 0x5A, sizeof(window));
    assert(!flac_frame_find_sync(window, sizeof(window), &info, &offset));
}

static void test_a_frame_from_another_stream_is_not_this_stream(void)
{
    /* The check that made the difference over a whole album: a header can pass
     * its own CRC and still belong to nothing - so it is measured against the
     * stream it claims to be part of. This one says one channel where
     * STREAMINFO says two. */
    flac_streaminfo_t info;
    assert(flac_streaminfo_parse(streaminfo_sample, sizeof(streaminfo_sample), &info));
    info.channels = 1U;

    size_t offset = 0U;
    assert(!flac_frame_find_sync(frame_header_sample, sizeof(frame_header_sample), &info,
                                 &offset));
    // And without a stream to check against, the header stands on its own.
    assert(flac_frame_find_sync(frame_header_sample, sizeof(frame_header_sample), NULL,
                                &offset));
    assert(offset == 0U);
}

static void test_a_block_that_is_not_streaminfo_is_refused(void)
{
    flac_streaminfo_t info;
    // Short: the format fixes STREAMINFO at 34 bytes, and a shorter one is
    // something else being read as one.
    assert(!flac_streaminfo_parse(streaminfo_sample, FLAC_STREAMINFO_SIZE - 1U, &info));
    assert(!flac_streaminfo_parse(NULL, FLAC_STREAMINFO_SIZE, &info));
    assert(!flac_streaminfo_parse(streaminfo_sample, FLAC_STREAMINFO_SIZE, NULL));
    // A rate of zero is impossible in FLAC, and every length divides by it.
    uint8_t zero_rate[FLAC_STREAMINFO_SIZE] = {0};
    assert(!flac_streaminfo_parse(zero_rate, sizeof(zero_rate), &info));
}

int main(void)
{
    test_the_block_header_splits_into_three_parts();
    test_vorbis_comments_are_named_fields();
    test_a_truncated_comment_block_stops_rather_than_reads_on();
    test_a_picture_block_is_described_without_its_image();
    test_a_picture_header_that_runs_past_the_block_is_refused();
    test_the_signature_is_four_bytes();
    test_streaminfo_reads_the_numbers_a_track_is_measured_by();
    test_streaminfo_fields_are_not_on_byte_boundaries();
    test_a_block_that_is_not_streaminfo_is_refused();
    test_a_frame_header_is_read_with_its_crc();
    test_a_pattern_that_only_looks_like_a_header_is_refused();
    test_the_sync_is_found_where_a_jump_lands();
    test_a_frame_from_another_stream_is_not_this_stream();
    printf("flac_tags tests passed\n");
    return 0;
}
