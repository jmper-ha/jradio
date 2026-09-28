#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "rds.h"

#define PI 0x7201U

/* Another group of the station's, of a type nothing here reads: what a
 * station sends between the groups a test is about. */
static void filler(rds_decoder_t *decoder)
{
    static uint16_t count;
    const uint16_t blocks[4] = {PI, 0xF000U, 0U, ++count};
    (void)rds_decoder_feed(decoder, blocks, true, true);
}

/* Group 0A, segment `segment`, two characters - with a group of something
 * else after it, as on the air, so the next one is not a second read of it. */
static void ps(rds_decoder_t *decoder, unsigned segment, const char *two)
{
    const uint16_t blocks[4] = {PI, (uint16_t)(0x0000U | segment), 0U,
                                (uint16_t)(((uint8_t)two[0] << 8) | (uint8_t)two[1])};
    (void)rds_decoder_feed(decoder, blocks, true, true);
    filler(decoder);
}

static void ps_name(rds_decoder_t *decoder, const char *eight)
{
    for (unsigned segment = 0U; segment < 4U; ++segment) ps(decoder, segment, eight + segment * 2U);
}

/* Group 2A, text flag `flag`, segment `segment`, four characters - once. */
static void rt_once(rds_decoder_t *decoder, unsigned flag, unsigned segment, const char *four)
{
    const uint16_t blocks[4] = {
        PI, (uint16_t)(0x2000U | (flag << 4) | segment),
        (uint16_t)(((uint8_t)four[0] << 8) | (uint8_t)four[1]),
        (uint16_t)(((uint8_t)four[2] << 8) | (uint8_t)four[3])};
    (void)rds_decoder_feed(decoder, blocks, true, true);
    filler(decoder);
}

// The way a station sends it: every piece comes round more than once.
static void rt(rds_decoder_t *decoder, unsigned flag, unsigned segment, const char *four)
{
    rt_once(decoder, flag, segment, four);
    rt_once(decoder, flag, segment, four);
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

static void test_a_settled_station_is_not_replaced(void)
{
    rds_decoder_t decoder;
    rds_decoder_reset(&decoder);
    ps_name(&decoder, "FIRST   ");
    ps_name(&decoder, "FIRST   ");
    assert(strcmp(decoder.ps_text, "FIRST") == 0);
    assert(rds_decoder_heard(&decoder));
    /* A code out of a damaged block A is nothing. */
    const uint16_t noise[4] = {0x3495U, 0x0000U, 0U, ('F' << 8) | 'I'};
    (void)rds_decoder_feed(&decoder, noise, false, true);
    assert(strcmp(decoder.ps_text, "FIRST") == 0);
    /* Nor is a burst of noise with the same wrong code twice running, which
       the chip calls clean: once the station is settled, only a retune (a
       reset) starts another. */
    const uint16_t burst[4] = {0xE28CU, 0x0000U, 0U, ('S' << 8) | 'E'};
    const uint16_t burst_next[4] = {0xE28CU, 0x0001U, 0U, ('C' << 8) | 'O'};
    (void)rds_decoder_feed(&decoder, burst, true, true);
    (void)rds_decoder_feed(&decoder, burst_next, true, true);
    (void)rds_decoder_feed(&decoder, burst, true, true);
    assert(strcmp(decoder.ps_text, "FIRST") == 0);
    assert(decoder.pi == PI);
}

static void test_the_first_code_must_come_twice_before_it_settles(void)
{
    rds_decoder_t decoder;
    rds_decoder_reset(&decoder);
    // Noise first: taken as the code, but not settled.
    const uint16_t noise[4] = {0x3495U, 0x0000U, 0U, ('X' << 8) | 'X'};
    (void)rds_decoder_feed(&decoder, noise, true, true);
    assert(decoder.pi == 0x3495U && !rds_decoder_heard(&decoder));
    // The real station, twice running, takes over.
    filler(&decoder);
    filler(&decoder);
    assert(decoder.pi == PI);
}

static void test_the_station_is_heard_by_its_code(void)
{
    rds_decoder_t decoder;
    rds_decoder_reset(&decoder);
    assert(!rds_decoder_heard(&decoder));
    // Noise: a code each time, never the same twice.
    const uint16_t codes[] = {0x3495U, 0xC9ABU, 0x93C0U, 0x9CEEU};
    for (size_t i = 0; i < 4; ++i) {
        const uint16_t blocks[4] = {codes[i], 0x0000U, 0U, 0U};
        (void)rds_decoder_feed(&decoder, blocks, true, true);
    }
    assert(!rds_decoder_heard(&decoder));
    // A station: its code three times running, in whatever groups.
    rds_decoder_reset(&decoder);
    filler(&decoder);
    filler(&decoder);
    assert(!rds_decoder_heard(&decoder));
    filler(&decoder);
    assert(rds_decoder_heard(&decoder));
    assert(decoder.ps_text[0] == '\0');  // long before any name
    rds_decoder_reset(&decoder);
    assert(!rds_decoder_heard(&decoder));
}

static void test_noise_puts_nothing_into_the_text(void)
{
    rds_decoder_t decoder;
    rds_decoder_reset(&decoder);
    rt(&decoder, 0U, 0U, "Now ");
    rt(&decoder, 0U, 1U, "on\r ");
    assert(strcmp(decoder.rt_text, "Now on") == 0);
    // One spoiled sighting of a piece is not believed.
    rt_once(&decoder, 0U, 1U, "o\x93\xfa\r");
    assert(strcmp(decoder.rt_text, "Now on") == 0);
    /* Nor is a whole group under another code - noise comes that way, with a
       different code each time. */
    const uint16_t noise[4] = {0x9468U, 0x2001U, ('X' << 8) | 'Y', ('\r' << 8) | ' '};
    const uint16_t more_noise[4] = {0x1444U, 0x2001U, ('X' << 8) | 'Y', ('\r' << 8) | ' '};
    (void)rds_decoder_feed(&decoder, noise, true, true);
    (void)rds_decoder_feed(&decoder, more_noise, true, true);
    assert(strcmp(decoder.rt_text, "Now on") == 0);
    assert(decoder.pi == PI);
}

static void test_a_text_the_station_stops_sending_goes(void)
{
    rds_decoder_t decoder;
    rds_decoder_reset(&decoder);
    rt(&decoder, 0U, 0U, "Song");
    rt(&decoder, 0U, 1U, "\r   ");
    assert(strcmp(decoder.rt_text, "Song") == 0);
    /* Its other groups keep coming, its text does not. rt() has already sent
       one group of something else after the text. */
    for (unsigned group = 0U; group + 2U < RDS_RT_STALE_GROUPS; ++group) filler(&decoder);
    assert(strcmp(decoder.rt_text, "Song") == 0);
    filler(&decoder);
    assert(decoder.rt_text[0] == '\0');
    // And a new text comes up again as before.
    rt(&decoder, 1U, 0U, "Next");
    rt(&decoder, 1U, 1U, "\r   ");
    assert(strcmp(decoder.rt_text, "Next") == 0);
}

static void test_a_group_read_twice_counts_once(void)
{
    rds_decoder_t decoder;
    rds_decoder_reset(&decoder);
    rt(&decoder, 0U, 0U, "Song");
    rt(&decoder, 0U, 1U, "\r   ");
    assert(strcmp(decoder.rt_text, "Song") == 0);
    /* A noisy group under a stray code, read twice because the chip's flag
       was still up: it is not another station, and nothing is lost. */
    const uint16_t noise[4] = {0xEE60U, 0x0000U, 0U, ('X' << 8) | 'Y'};
    (void)rds_decoder_feed(&decoder, noise, true, true);
    (void)rds_decoder_feed(&decoder, noise, true, true);
    assert(strcmp(decoder.rt_text, "Song") == 0);
    assert(decoder.pi == PI);
    /* Nor does a piece read twice get believed. */
    const uint16_t piece[4] = {PI, 0x2001U, ('Z' << 8) | 'Z', ('\r' << 8) | ' '};
    (void)rds_decoder_feed(&decoder, piece, true, true);
    (void)rds_decoder_feed(&decoder, piece, true, true);
    assert(strcmp(decoder.rt_text, "Song") == 0);
}

static void test_a_settled_noise_code_gives_way_to_the_station(void)
{
    rds_decoder_t decoder;
    rds_decoder_reset(&decoder);
    /* A burst of noise under one code, three groups running, settles first -
       what a morning switch-on can meet. */
    for (uint16_t i = 0U; i < 3U; ++i) {
        const uint16_t burst[4] = {0xE28CU, 0xF000U, 0U, i};
        (void)rds_decoder_feed(&decoder, burst, true, true);
    }
    assert(decoder.pi == 0xE28CU && rds_decoder_heard(&decoder));
    /* Then the station, with noise of other codes among its groups: it takes
       over, and its name comes through. */
    for (int round = 0; round < 8; ++round) {
        filler(&decoder);
        const uint16_t other[4] = {(uint16_t)(0x1000U + round), 0xF000U, 0U, 0U};
        (void)rds_decoder_feed(&decoder, other, true, true);
        filler(&decoder);
    }
    assert(decoder.pi == PI);
    ps_name(&decoder, "RADIOMSK");
    ps_name(&decoder, "RADIOMSK");
    assert(strcmp(decoder.ps_text, "RADIOMSK") == 0);
}

static void test_spoiled_bytes_are_not_taken(void)
{
    rds_decoder_t decoder;
    rds_decoder_reset(&decoder);
    // The same spoiled piece twice: bytes no station sends.
    const char spoiled[3] = {(char)0x93, (char)0x88, '\0'};
    ps(&decoder, 0U, spoiled);
    ps(&decoder, 0U, spoiled);
    ps_name(&decoder, "RADIOMSK");
    ps_name(&decoder, "RADIOMSK");
    assert(strcmp(decoder.ps_text, "RADIOMSK") == 0);
    rt(&decoder, 0U, 0U, "\x01\x02" "ab");
    assert(decoder.rt_candidates == 0U);
}

int main(void)
{
    test_a_settled_noise_code_gives_way_to_the_station();
    test_spoiled_bytes_are_not_taken();
    test_a_group_read_twice_counts_once();
    test_a_text_the_station_stops_sending_goes();
    test_noise_puts_nothing_into_the_text();
    test_the_station_is_heard_by_its_code();
    test_the_name_needs_every_piece_twice();
    test_a_scrolling_name_follows();
    test_two_alternating_names_never_mix();
    test_a_group_with_a_bad_block_b_is_dropped();
    test_windows_1251_is_read_as_cyrillic();
    test_radiotext_shows_once_it_is_whole();
    test_a_settled_station_is_not_replaced();
    test_the_first_code_must_come_twice_before_it_settles();
    puts("rds tests passed");
    return 0;
}
