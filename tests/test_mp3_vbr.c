#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "mp3_vbr.h"

/* A first frame as LAME writes it: MPEG-1 Layer III, 44.1 kHz, joint stereo,
 * a "Xing" header behind 32 bytes of side information, with all three of the
 * frame count, the byte count and the table of contents. */
static size_t xing_frame(uint8_t *out, const char *tag, uint8_t mode_bits)
{
    memset(out, 0, 512);
    out[0] = 0xFF;
    out[1] = 0xFB;           // MPEG-1, Layer III, no CRC
    out[2] = 0x90;           // 128 kbit/s, 44.1 kHz
    out[3] = mode_bits;      // 0x44 joint stereo, 0xC4 mono
    const size_t xing = 4U + (mode_bits == 0xC4 ? 17U : 32U);
    memcpy(out + xing, tag, 4);
    out[xing + 7] = 0x07;    // frames, bytes, TOC
    // 9209 frames, 8,600,000 bytes.
    out[xing + 10] = 0x23;
    out[xing + 11] = 0xF9;
    out[xing + 12] = 0x00;
    out[xing + 13] = 0x83;
    out[xing + 14] = 0x39;
    out[xing + 15] = 0xC0;
    for (int i = 0; i < 100; ++i) out[xing + 16 + i] = (uint8_t)(i * 256 / 100);
    out[xing + 16 + 50] = 100;  // a bend at the middle, to see the table used
    return 417;
}

static void test_a_lame_header_gives_length_and_average(void)
{
    uint8_t frame[512];
    xing_frame(frame, "Xing", 0x44);
    mp3_vbr_t vbr;
    assert(mp3_vbr_parse(frame, sizeof(frame), &vbr));
    assert(vbr.frames == 9209U);
    assert(vbr.bytes == 8600000U);
    assert(vbr.sample_rate == 44100U);
    assert(vbr.samples_per_frame == 1152U);
    assert(vbr.has_toc);
    assert(vbr.frame_offset == 0U);
    // 9209 x 1152 / 44100 = 240.56 s, the length the file really has.
    assert(mp3_vbr_duration_ms(&vbr) == 240561U);

    // LAME's constant-rate header reads the same.
    xing_frame(frame, "Info", 0x44);
    assert(mp3_vbr_parse(frame, sizeof(frame), &vbr) && vbr.frames == 9209U);
    // A mono file's header sits behind 17 bytes of side information.
    xing_frame(frame, "Xing", 0xC4);
    assert(mp3_vbr_parse(frame, sizeof(frame), &vbr) && vbr.frames == 9209U);
}

static void test_a_jump_follows_the_table(void)
{
    uint8_t frame[512];
    xing_frame(frame, "Xing", 0x44);
    mp3_vbr_t vbr;
    assert(mp3_vbr_parse(frame, sizeof(frame), &vbr));
    const uint64_t total = mp3_vbr_duration_ms(&vbr);
    assert(mp3_vbr_seek_offset(&vbr, 0U) == 0U);
    // Half way through the time is where the table says, 100/256 of the
    // bytes - not half the file.
    assert(mp3_vbr_seek_offset(&vbr, (total * 50U + 99U) / 100U) == 100ULL * 8600000U / 256U);
    assert(mp3_vbr_seek_offset(&vbr, total) == 8600000U);
    // Without a table, in proportion.
    vbr.has_toc = false;
    assert(mp3_vbr_seek_offset(&vbr, total / 4U) == (total / 4U) * 8600000U / total);
}

static void test_an_id3_tag_is_stepped_over(void)
{
    uint8_t tag[20] = {'I', 'D', '3', 4, 0, 0, 0, 0, 0x02, 0x01};
    assert(mp3_id3v2_length(tag, sizeof(tag)) == 10U + 257U);
    tag[5] = 0x10;  // a footer
    assert(mp3_id3v2_length(tag, sizeof(tag)) == 10U + 257U + 10U);
    tag[8] = 0x80;  // not a sync-safe size
    assert(mp3_id3v2_length(tag, sizeof(tag)) == 0U);
    assert(mp3_id3v2_length((const uint8_t *)"TAG", 3) == 0U);
}

static void test_a_file_without_a_header_is_left_to_its_frames(void)
{
    uint8_t frame[512] = {0xFF, 0xFB, 0x90, 0x44};
    mp3_vbr_t vbr;
    assert(!mp3_vbr_parse(frame, sizeof(frame), &vbr));
    // A frame found after some junk: the junk is not audio.
    uint8_t shifted[600] = {0};
    xing_frame(shifted + 37, "Xing", 0x44);
    assert(mp3_vbr_parse(shifted, sizeof(shifted), &vbr) && vbr.frame_offset == 37U);
    // Cut short before the counts.
    xing_frame(frame, "Xing", 0x44);
    assert(!mp3_vbr_parse(frame, 40U, &vbr));
    assert(!mp3_vbr_parse(NULL, 0U, &vbr));
}

int main(void)
{
    test_a_lame_header_gives_length_and_average();
    test_a_jump_follows_the_table();
    test_an_id3_tag_is_stepped_over();
    test_a_file_without_a_header_is_left_to_its_frames();
    printf("mp3 vbr tests passed\n");
    return 0;
}
