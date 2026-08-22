#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "audio_tags_reader.h"
#include "flac_tags.h"
#include "fixtures/id3v2_sample.h"
#include "id3v2.h"

#define SCRATCH_SIZE 4096U

static FILE *file_of(const uint8_t *bytes, size_t length)
{
    FILE *file = tmpfile();
    assert(file != NULL);
    assert(fwrite(bytes, 1U, length, file) == length);
    assert(fflush(file) == 0);
    return file;
}

typedef struct {
    uint8_t data[4096];
    size_t length;
} builder_t;

static void put(builder_t *builder, const void *bytes, size_t length)
{
    assert(builder->length + length <= sizeof(builder->data));
    memcpy(builder->data + builder->length, bytes, length);
    builder->length += length;
}

static void put_be32(builder_t *builder, uint32_t value)
{
    const uint8_t bytes[4] = {(uint8_t)(value >> 24), (uint8_t)(value >> 16),
                              (uint8_t)(value >> 8), (uint8_t)value};
    put(builder, bytes, sizeof(bytes));
}

static void put_le32(builder_t *builder, uint32_t value)
{
    const uint8_t bytes[4] = {(uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16),
                              (uint8_t)(value >> 24)};
    put(builder, bytes, sizeof(bytes));
}

static void put_syncsafe(builder_t *builder, uint32_t value)
{
    const uint8_t bytes[4] = {(uint8_t)((value >> 21) & 0x7FU), (uint8_t)((value >> 14) & 0x7FU),
                              (uint8_t)((value >> 7) & 0x7FU), (uint8_t)(value & 0x7FU)};
    put(builder, bytes, sizeof(bytes));
}

static void test_the_real_file_gives_up_its_text(void)
{
    FILE *file = file_of(id3v2_sample_tag, ID3V2_SAMPLE_TAG_LENGTH);
    uint8_t scratch[SCRATCH_SIZE];
    audio_tags_t tags;
    assert(audio_tags_read_file(file, scratch, sizeof(scratch), &tags));
    assert(strcmp(tags.title, "01-1 Балтийское море") == 0);
    assert(strcmp(tags.artist, "Сахарнов С., Орлов О., Бирман Н.") == 0);
    /* The tag declares 68325 bytes and only 1024 are here, which is the shape
     * of every read where the cover does not fit: the text still arrives. */
    assert(tags.picture_length == 0U);
    fclose(file);
}

static void test_a_buffer_too_small_for_all_the_frames_keeps_the_early_ones(void)
{
    /* TIT2 sits behind six other frames in these files, so a 700-byte read
     * reaches the album and the performer and stops short of the title. Losing
     * one line is worth more than failing the read. */
    FILE *file = file_of(id3v2_sample_tag, ID3V2_SAMPLE_TAG_LENGTH);
    uint8_t scratch[700];
    audio_tags_t tags;
    assert(audio_tags_read_file(file, scratch, sizeof(scratch), &tags));
    assert(strcmp(tags.artist, "Сахарнов С., Орлов О., Бирман Н.") == 0);
    assert(tags.title[0] == '\0');
    fclose(file);
}

static void test_a_cover_that_fits_comes_back_in_the_scratch(void)
{
    builder_t builder = {.length = 0U};
    const char *image = "\xFF\xD8\xFF\xE0JPEG";
    const size_t picture_body = 1U + strlen("image/jpeg") + 1U + 1U + 1U + strlen(image);
    const size_t title_body = 1U + strlen("Title");
    const size_t tag_body = 10U + title_body + 10U + picture_body;

    put(&builder, "ID3", 3U);
    put(&builder, "\x03\x00\x00", 3U);
    put_syncsafe(&builder, (uint32_t)tag_body);
    put(&builder, "TIT2", 4U);
    put_be32(&builder, (uint32_t)title_body);
    put(&builder, "\x00\x00", 2U);
    put(&builder, "\x00", 1U);
    put(&builder, "Title", 5U);
    put(&builder, "APIC", 4U);
    put_be32(&builder, (uint32_t)picture_body);
    put(&builder, "\x00\x00", 2U);
    put(&builder, "\x00", 1U);
    put(&builder, "image/jpeg", strlen("image/jpeg") + 1U);
    put(&builder, "\x03", 1U);
    put(&builder, "\x00", 1U);
    put(&builder, image, strlen(image));
    // Whatever follows the tag is audio, and must not be mistaken for more of
    // it.
    put(&builder, "\xFF\xFB\x90\x00", 4U);

    FILE *file = file_of(builder.data, builder.length);
    uint8_t scratch[SCRATCH_SIZE];
    audio_tags_t tags;
    assert(audio_tags_read_file(file, scratch, sizeof(scratch), &tags));
    assert(strcmp(tags.title, "Title") == 0);
    assert(tags.picture_format == AUDIO_TAGS_PICTURE_JPEG);
    assert(tags.picture_length == strlen(image));
    assert(memcmp(scratch + tags.picture_offset, image, tags.picture_length) == 0);
    fclose(file);
}

static void test_a_flac_file_walks_its_metadata_blocks(void)
{
    builder_t comment = {.length = 0U};
    put_le32(&comment, 0U);
    put_le32(&comment, 2U);
    const char *title = "TITLE=Балтийское море";
    put_le32(&comment, (uint32_t)strlen(title));
    put(&comment, title, strlen(title));
    const char *artist = "ARTIST=Сахарнов";
    put_le32(&comment, (uint32_t)strlen(artist));
    put(&comment, artist, strlen(artist));

    // Split so the hex escape cannot swallow the letter after it.
    const char *image = "\xFF\xD8\xFF\xE0" "FLACJPEG";
    builder_t picture = {.length = 0U};
    put_be32(&picture, 3U);
    put_be32(&picture, (uint32_t)strlen("image/jpeg"));
    put(&picture, "image/jpeg", strlen("image/jpeg"));
    put_be32(&picture, 0U);
    put_be32(&picture, 250U);
    put_be32(&picture, 250U);
    put_be32(&picture, 24U);
    put_be32(&picture, 0U);
    put_be32(&picture, (uint32_t)strlen(image));
    put(&picture, image, strlen(image));

    builder_t builder = {.length = 0U};
    put(&builder, "fLaC", 4U);
    // STREAMINFO first, as every FLAC file has it, so the walk has something
    // to step over before it finds anything it wants.
    const uint8_t streaminfo_header[4] = {0x00U, 0x00U, 0x00U, 0x22U};
    const uint8_t streaminfo[0x22] = {0};
    put(&builder, streaminfo_header, sizeof(streaminfo_header));
    put(&builder, streaminfo, sizeof(streaminfo));

    const uint8_t comment_header[4] = {FLAC_BLOCK_VORBIS_COMMENT, (uint8_t)(comment.length >> 16),
                                       (uint8_t)(comment.length >> 8),
                                       (uint8_t)comment.length};
    put(&builder, comment_header, sizeof(comment_header));
    put(&builder, comment.data, comment.length);

    const uint8_t picture_header[4] = {0x80U | FLAC_BLOCK_PICTURE,
                                       (uint8_t)(picture.length >> 16),
                                       (uint8_t)(picture.length >> 8), (uint8_t)picture.length};
    put(&builder, picture_header, sizeof(picture_header));
    put(&builder, picture.data, picture.length);

    FILE *file = file_of(builder.data, builder.length);
    uint8_t scratch[SCRATCH_SIZE];
    audio_tags_t tags;
    assert(audio_tags_read_file(file, scratch, sizeof(scratch), &tags));
    assert(strcmp(tags.title, "Балтийское море") == 0);
    assert(strcmp(tags.artist, "Сахарнов") == 0);
    assert(tags.picture_format == AUDIO_TAGS_PICTURE_JPEG);
    assert(tags.picture_length == strlen(image));
    assert(memcmp(scratch + tags.picture_offset, image, tags.picture_length) == 0);
    fclose(file);
}

static void test_a_cover_too_large_for_the_scratch_is_still_found(void)
{
    /* The case that showed the placeholder over a picture the file was
     * carrying: a 140 KB cover read through a 128 KB buffer. Here the same
     * shape in miniature - a picture larger than the scratch - and what
     * matters is that it is not given up on, but pointed at where it lies. */
    builder_t picture = {.length = 0U};
    put_be32(&picture, 3U);
    put_be32(&picture, (uint32_t)strlen("image/jpeg"));
    put(&picture, "image/jpeg", strlen("image/jpeg"));
    put_be32(&picture, 0U);
    put_be32(&picture, 250U);
    put_be32(&picture, 250U);
    put_be32(&picture, 24U);
    put_be32(&picture, 0U);
    const size_t image_length = 2048U;
    put_be32(&picture, (uint32_t)image_length);
    const size_t image_at = picture.length;
    for (size_t index = 0U; index < image_length; ++index) {
        const uint8_t byte = (uint8_t)(index & 0xFFU);
        put(&picture, &byte, 1U);
    }

    builder_t builder = {.length = 0U};
    put(&builder, "fLaC", 4U);
    const uint8_t picture_header[4] = {0x80U | FLAC_BLOCK_PICTURE,
                                       (uint8_t)(picture.length >> 16),
                                       (uint8_t)(picture.length >> 8), (uint8_t)picture.length};
    put(&builder, picture_header, sizeof(picture_header));
    put(&builder, picture.data, picture.length);

    FILE *file = file_of(builder.data, builder.length);
    // Deliberately smaller than the picture, the way the device's buffer is
    // smaller than the cover that motivated this.
    uint8_t scratch[1024];
    audio_tags_t tags;
    assert(audio_tags_read_file(file, scratch, sizeof(scratch), &tags));
    assert(tags.picture_format == AUDIO_TAGS_PICTURE_JPEG);
    // Not in the scratch - it never fitted - but locatable in the file.
    assert(tags.picture_length == 0U);
    assert(tags.picture_file_length == image_length);
    assert(tags.picture_file_offset == 4U + 4U + image_at);

    // And the bytes there really are the picture.
    uint8_t first[4];
    assert(fseek(file, (long)tags.picture_file_offset, SEEK_SET) == 0);
    assert(fread(first, 1U, sizeof(first), file) == sizeof(first));
    assert(first[0] == 0x00U && first[1] == 0x01U && first[2] == 0x02U && first[3] == 0x03U);
    fclose(file);
}

static void test_a_tag_cut_off_inside_its_picture_reports_none(void)
{
    /* The new fields must not turn "no picture" into a picture at offset
     * zero: a caller that trusts a length of zero to mean nothing is there
     * would otherwise read the front of the file as an image. */
    FILE *file = file_of(id3v2_sample_tag, ID3V2_SAMPLE_TAG_LENGTH);
    uint8_t scratch[SCRATCH_SIZE];
    audio_tags_t tags;
    (void)audio_tags_read_file(file, scratch, sizeof(scratch), &tags);
    // The fixture's tag is cut off inside its APIC frame, so this file has no
    // usable picture at all - which must not read as one at offset zero.
    assert(tags.picture_length == 0U);
    assert(tags.picture_file_length == 0U);
    fclose(file);
}

static void test_id3v1_at_the_end_is_read_when_nothing_else_is_there(void)
{
    builder_t builder = {.length = 0U};
    const uint8_t audio[512] = {0xFFU, 0xFBU};
    put(&builder, audio, sizeof(audio));
    uint8_t block[ID3V1_BLOCK_SIZE];
    memset(block, 0, sizeof(block));
    memcpy(block, "TAG", 3U);
    memcpy(block + 3, "Old Title", 9U);
    memcpy(block + 33, "Old Artist", 10U);
    put(&builder, block, sizeof(block));

    FILE *file = file_of(builder.data, builder.length);
    uint8_t scratch[SCRATCH_SIZE];
    audio_tags_t tags;
    assert(audio_tags_read_file(file, scratch, sizeof(scratch), &tags));
    assert(strcmp(tags.title, "Old Title") == 0);
    assert(strcmp(tags.artist, "Old Artist") == 0);
    fclose(file);
}

static void test_an_untagged_file_reports_nothing_rather_than_leftovers(void)
{
    const uint8_t audio[512] = {0xFFU, 0xFBU, 0x90U, 0x00U};
    FILE *file = file_of(audio, sizeof(audio));
    uint8_t scratch[SCRATCH_SIZE];
    audio_tags_t tags;
    // Filled first, because the caller reuses one structure across tracks and
    // a stale title under a new file is worse than no title.
    memset(&tags, 'x', sizeof(tags));
    assert(!audio_tags_read_file(file, scratch, sizeof(scratch), &tags));
    assert(!audio_tags_have_text(&tags));
    assert(tags.picture_length == 0U);
    fclose(file);
}

int main(void)
{
    test_the_real_file_gives_up_its_text();
    test_a_buffer_too_small_for_all_the_frames_keeps_the_early_ones();
    test_a_cover_that_fits_comes_back_in_the_scratch();
    test_a_flac_file_walks_its_metadata_blocks();
    test_a_cover_too_large_for_the_scratch_is_still_found();
    test_a_tag_cut_off_inside_its_picture_reports_none();
    test_id3v1_at_the_end_is_read_when_nothing_else_is_there();
    test_an_untagged_file_reports_nothing_rather_than_leftovers();
    printf("audio_tags_reader tests passed\n");
    return 0;
}
