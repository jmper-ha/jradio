#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "rds.h"

#define PI 0x7201U

/* Group 0A, segment `segment`, two characters. */
static void ps(rds_decoder_t *decoder, unsigned segment, const char *two)
{
    const uint16_t blocks[4] = {PI, (uint16_t)(0x0000U | segment), 0U,
                                (uint16_t)(((uint8_t)two[0] << 8) | (uint8_t)two[1])};
    (void)rds_decoder_feed(decoder, blocks, true, true);
}

static void ps_name(rds_decoder_t *decoder, const char *eight)
{
    for (unsigned segment = 0U; segment < 4U; ++segment) ps(decoder, segment, eight + segment * 2U);
}

/* Group 2A, text flag `flag`, segment `segment`, four characters. */
static void rt(rds_decoder_t *decoder, unsigned flag, unsigned segment, const char *four)
{
    const uint16_t blocks[4] = {
        PI, (uint16_t)(0x2000U | (flag << 4) | segment),
        (uint16_t)(((uint8_t)four[0] << 8) | (uint8_t)four[1]),
        (uint16_t)(((uint8_t)four[2] << 8) | (uint8_t)four[3])};
    (void)rds_decoder_feed(decoder, blocks, true, true);
}

static const uint16_t *blocks_of_pi(uint16_t pi)
{
    // A group of a type nothing here reads: it only carries the code.
    static uint16_t blocks[4];
    blocks[0] = pi;
    blocks[1] = 0xF000U;
    blocks[2] = 0U;
    blocks[3] = 0U;
    return blocks;
}

static void test_the_name_needs_every_piece_twice(void)
{
    rds_decoder_t decoder;
    rds_decoder_reset(&decoder);
    ps_name(&decoder, "EUROPA  ");
    assert(decoder.ps_text[0] == '\0');  // once is not enough
    const uint32_t before = decoder.revision;
    ps_name(&decoder, "EUROPA  ");
    assert(strcmp(decoder.ps_text, "EUROPA") == 0);  // padding trimmed
    assert(decoder.revision == before + 1U);

    /* One bad block D says something else once; it does not get in. */
    ps(&decoder, 1U, "XX");
    assert(strcmp(decoder.ps_text, "EUROPA") == 0);
    ps(&decoder, 1U, "RO");
    assert(strcmp(decoder.ps_text, "EUROPA") == 0);
}

static void test_a_scrolling_name_follows(void)
{
    rds_decoder_t decoder;
    rds_decoder_reset(&decoder);
    ps_name(&decoder, "RADIO 1 ");
    ps_name(&decoder, "RADIO 1 ");
    ps(&decoder, 3U, "!!");
    ps(&decoder, 3U, "!!");
    /* Not yet: the other three pieces could be from the old message. */
    assert(strcmp(decoder.ps_text, "RADIO 1") == 0);
    ps_name(&decoder, "RADIO !!");
    ps_name(&decoder, "RADIO !!");
    assert(strcmp(decoder.ps_text, "RADIO !!") == 0);
    /* The blank message between names leaves the last one up. */
    ps_name(&decoder, "        ");
    ps_name(&decoder, "        ");
    assert(strcmp(decoder.ps_text, "RADIO !!") == 0);
}

static void test_two_alternating_names_never_mix(void)
{
    /* A station that sends its frequency and its name in turn: whatever is
       shown is one of them, whole. */
    rds_decoder_t decoder;
    rds_decoder_reset(&decoder);
    const char *names[] = {"*95.6FM*", "        ", "*ZVEZDA*", "        ", "*3BE3DA*"};
    bool shown[3] = {false, false, false};
    for (int round = 0; round < 10; ++round) {
        const char *name = names[round % 5];
        // Each message goes round several times, a group per segment.
        for (int repeat = 0; repeat < 3; ++repeat) {
            for (unsigned segment = 0U; segment < 4U; ++segment) {
                ps(&decoder, segment, name + segment * 2U);
                assert(decoder.ps_text[0] == '\0' || strcmp(decoder.ps_text, "*95.6FM*") == 0 ||
                       strcmp(decoder.ps_text, "*ZVEZDA*") == 0 ||
                       strcmp(decoder.ps_text, "*3BE3DA*") == 0);
                shown[0] |= strcmp(decoder.ps_text, "*95.6FM*") == 0;
                shown[1] |= strcmp(decoder.ps_text, "*ZVEZDA*") == 0;
                shown[2] |= strcmp(decoder.ps_text, "*3BE3DA*") == 0;
            }
        }
    }
    // And each of them does come up whole.
    assert(shown[0] && shown[1] && shown[2]);
}

static void test_a_group_with_a_bad_block_b_is_dropped(void)
{
    rds_decoder_t decoder;
    rds_decoder_reset(&decoder);
    const uint16_t blocks[4] = {PI, 0x0000U, 0U, ('A' << 8) | 'B'};
    for (int i = 0; i < 4; ++i) assert(!rds_decoder_feed(&decoder, blocks, true, false));
    assert(decoder.ps_candidates == 0U);
}

static void test_windows_1251_is_read_as_cyrillic(void)
{
    rds_decoder_t decoder;
    rds_decoder_reset(&decoder);
    // "Шоколад" in windows-1251, and a space.
    const char name[9] = {(char)0xD8, (char)0xEE, (char)0xEA, (char)0xEE,
                          (char)0xEB, (char)0xE0, (char)0xE4, ' ', '\0'};
    ps_name(&decoder, name);
    ps_name(&decoder, name);
    assert(strcmp(decoder.ps_text, "Шоколад") == 0);
}

static void test_radiotext_shows_once_it_is_whole(void)
{
    rds_decoder_t decoder;
    rds_decoder_reset(&decoder);
    rt(&decoder, 0U, 0U, "Now ");
    rt(&decoder, 0U, 2U, "Rain");
    assert(decoder.rt_text[0] == '\0');  // segment 1 is missing
    rt(&decoder, 0U, 1U, "on: ");
    rt(&decoder, 0U, 3U, "\r   ");
    assert(strcmp(decoder.rt_text, "Now on: Rain") == 0);

    /* The station flips its flag for the next text: the old one is dropped
       from what is collected, and shown until the new one is whole. */
    rt(&decoder, 1U, 0U, "Next");
    assert(strcmp(decoder.rt_text, "Now on: Rain") == 0);
    rt(&decoder, 1U, 1U, "\r   ");
    assert(strcmp(decoder.rt_text, "Next") == 0);
}

static void test_another_station_starts_again(void)
{
    rds_decoder_t decoder;
    rds_decoder_reset(&decoder);
    ps_name(&decoder, "FIRST   ");
    ps_name(&decoder, "FIRST   ");
    assert(strcmp(decoder.ps_text, "FIRST") == 0);
    const uint32_t before = decoder.revision;
    /* A code out of a damaged block A is not another station. */
    const uint16_t noise[4] = {0x3495U, 0x0000U, 0U, ('F' << 8) | 'I'};
    (void)rds_decoder_feed(&decoder, noise, false, true);
    assert(strcmp(decoder.ps_text, "FIRST") == 0);
    assert(decoder.pi == PI);
    /* Nor is one that the chip called clean but comes only once. */
    const uint16_t once[4] = {0xEE60U, 0x0000U, 0U, ('F' << 8) | 'I'};
    (void)rds_decoder_feed(&decoder, once, true, true);
    assert(strcmp(decoder.ps_text, "FIRST") == 0);
    (void)rds_decoder_feed(&decoder, blocks_of_pi(PI), true, true);
    const uint16_t other[4] = {0x7202U, 0x0000U, 0U, ('S' << 8) | 'E'};
    (void)rds_decoder_feed(&decoder, other, true, true);
    assert(strcmp(decoder.ps_text, "FIRST") == 0);
    (void)rds_decoder_feed(&decoder, other, true, true);
    assert(decoder.ps_text[0] == '\0');
    assert(decoder.revision != before);
    assert(decoder.pi == 0x7202U);
}

int main(void)
{
    test_the_name_needs_every_piece_twice();
    test_a_scrolling_name_follows();
    test_two_alternating_names_never_mix();
    test_a_group_with_a_bad_block_b_is_dropped();
    test_windows_1251_is_read_as_cyrillic();
    test_radiotext_shows_once_it_is_whole();
    test_another_station_starts_again();
    puts("rds tests passed");
    return 0;
}
