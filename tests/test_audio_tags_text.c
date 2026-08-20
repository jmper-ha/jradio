#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "audio_tags.h"

/* The files this was written for store their text as UTF-16 with a byte order
 * mark, which is what ID3v2.3 writers produce for anything outside Latin-1. */
static void test_utf16_with_a_little_endian_mark(void)
{
    static const uint8_t input[] = {0xFF, 0xFE, 0x1C, 0x04, 0x3E, 0x04,
                                    0x40, 0x04, 0x35, 0x04};
    char output[32];
    const size_t written = audio_tags_text_to_utf8(AUDIO_TAGS_ENCODING_UTF16_BOM, input,
                                                   sizeof(input), output, sizeof(output));
    assert(strcmp(output, "Море") == 0);
    assert(written == strlen("Море"));
}

static void test_utf16_with_a_big_endian_mark(void)
{
    static const uint8_t input[] = {0xFE, 0xFF, 0x04, 0x1C, 0x04, 0x3E,
                                    0x04, 0x40, 0x04, 0x35};
    char output[32];
    assert(audio_tags_text_to_utf8(AUDIO_TAGS_ENCODING_UTF16_BOM, input, sizeof(input),
                                   output, sizeof(output)) > 0U);
    assert(strcmp(output, "Море") == 0);
}

static void test_utf16_without_a_mark_is_read_as_little_endian(void)
{
    /* The mark is mandatory for this encoding and still goes missing. Guessing
     * from the zero high byte is the difference between a title and a column
     * of CJK glyphs. */
    static const uint8_t input[] = {0x1C, 0x04, 0x3E, 0x04, 0x40, 0x04, 0x35, 0x04};
    char output[32];
    assert(audio_tags_text_to_utf8(AUDIO_TAGS_ENCODING_UTF16_BOM, input, sizeof(input),
                                   output, sizeof(output)) > 0U);
    assert(strcmp(output, "Море") == 0);
}

static void test_declared_big_endian_needs_no_mark(void)
{
    static const uint8_t input[] = {0x04, 0x1C, 0x04, 0x3E, 0x04, 0x40, 0x04, 0x35};
    char output[32];
    assert(audio_tags_text_to_utf8(AUDIO_TAGS_ENCODING_UTF16_BE, input, sizeof(input),
                                   output, sizeof(output)) > 0U);
    assert(strcmp(output, "Море") == 0);
}

static void test_a_surrogate_pair_becomes_one_character(void)
{
    // U+1F3B5, a musical note - the one thing a tag editor is likely to insert
    // that does not fit in a single UTF-16 unit.
    static const uint8_t input[] = {0xFF, 0xFE, 0x3C, 0xD8, 0xB5, 0xDF};
    char output[16];
    const size_t written = audio_tags_text_to_utf8(AUDIO_TAGS_ENCODING_UTF16_BOM, input,
                                                   sizeof(input), output, sizeof(output));
    assert(written == 4U);
    assert(memcmp(output, "\xF0\x9F\x8E\xB5", 4U) == 0);
}

static void test_a_lone_surrogate_is_dropped(void)
{
    /* Half a pair is not a character. Encoding it anyway produces a three-byte
     * sequence that no font has and that LVGL draws as a box. */
    static const uint8_t input[] = {0xFF, 0xFE, 0x3C, 0xD8, 0x41, 0x00};
    char output[16];
    assert(audio_tags_text_to_utf8(AUDIO_TAGS_ENCODING_UTF16_BOM, input, sizeof(input),
                                   output, sizeof(output)) == 1U);
    assert(strcmp(output, "A") == 0);
}

static void test_windows_1251_pretending_to_be_latin1(void)
{
    /* Half the Russian MP3s in existence declare encoding 0 and then store
     * Windows-1251 bytes. Read literally they come out as "Áàëòèéñêîå". */
    static const uint8_t input[] = {0xC1, 0xE0, 0xEB, 0xF2, 0xE8,
                                    0xE9, 0xF1, 0xEA, 0xEE, 0xE5};
    assert(audio_tags_looks_like_cp1251(input, sizeof(input)));
    char output[32];
    assert(audio_tags_text_to_utf8(AUDIO_TAGS_ENCODING_ISO8859_1, input, sizeof(input),
                                   output, sizeof(output)) > 0U);
    assert(strcmp(output, "Балтийское") == 0);
}

static void test_a_single_accent_stays_latin1(void)
{
    /* The guess must not fire on real Latin-1. One accented letter in "Café"
     * is far more likely to be French than the start of a Russian word. */
    static const uint8_t input[] = {0x43, 0x61, 0x66, 0xE9};
    assert(!audio_tags_looks_like_cp1251(input, sizeof(input)));
    char output[32];
    assert(audio_tags_text_to_utf8(AUDIO_TAGS_ENCODING_ISO8859_1, input, sizeof(input),
                                   output, sizeof(output)) == 5U);
    assert(strcmp(output, "Café") == 0);
}

static void test_a_high_byte_outside_the_cyrillic_block_settles_it(void)
{
    // 0xB1 is "±" in both encodings and a letter in neither, so this cannot be
    // Russian text however many Cyrillic-looking bytes surround it.
    static const uint8_t input[] = {0xC1, 0xE0, 0xEB, 0xF2, 0xB1};
    assert(!audio_tags_looks_like_cp1251(input, sizeof(input)));
}

static void test_a_field_ends_at_its_first_zero(void)
{
    /* Frames are padded to their declared size, and the padding is not part of
     * the text. */
    static const uint8_t input[] = {'R', 'o', 'c', 'k', 0x00, 'j', 'u', 'n', 'k'};
    char output[32];
    assert(audio_tags_text_to_utf8(AUDIO_TAGS_ENCODING_ISO8859_1, input, sizeof(input),
                                   output, sizeof(output)) == 4U);
    assert(strcmp(output, "Rock") == 0);
}

static void test_control_characters_become_spaces(void)
{
    // A newline in a title makes a one-line label wrap and push the row below
    // it off the screen.
    static const uint8_t input[] = {'A', '\n', 'B', '\t', 'C'};
    char output[32];
    assert(audio_tags_text_to_utf8(AUDIO_TAGS_ENCODING_ISO8859_1, input, sizeof(input),
                                   output, sizeof(output)) == 5U);
    assert(strcmp(output, "A B C") == 0);
}

static void test_trailing_blanks_are_trimmed(void)
{
    static const uint8_t input[] = {'A', 'B', ' ', ' '};
    char output[32];
    assert(audio_tags_text_to_utf8(AUDIO_TAGS_ENCODING_ISO8859_1, input, sizeof(input),
                                   output, sizeof(output)) == 2U);
    assert(strcmp(output, "AB") == 0);
}

static void test_truncation_stops_on_a_character_boundary(void)
{
    /* Each of these is two bytes in UTF-8, so a buffer with room for five
     * payload bytes must stop after two of them rather than leaving half of
     * the third behind. */
    static const uint8_t input[] = {0xFF, 0xFE, 0x1C, 0x04, 0x3E, 0x04,
                                    0x40, 0x04, 0x35, 0x04};
    char output[6];
    const size_t written = audio_tags_text_to_utf8(AUDIO_TAGS_ENCODING_UTF16_BOM, input,
                                                   sizeof(input), output, sizeof(output));
    assert(written == 4U);
    assert(strcmp(output, "Мо") == 0);
}

static void test_utf8_input_is_bounded_and_checked(void)
{
    // Already the target encoding, but a memcpy would carry a truncated
    // sequence straight into a label.
    static const uint8_t good[] = {0xD0, 0x9C, 0xD0, 0xBE};
    char output[32];
    assert(audio_tags_text_to_utf8(AUDIO_TAGS_ENCODING_UTF8, good, sizeof(good), output,
                                   sizeof(output)) == 4U);
    assert(strcmp(output, "Мо") == 0);

    static const uint8_t cut[] = {0xD0, 0x9C, 0xD0};
    assert(audio_tags_text_to_utf8(AUDIO_TAGS_ENCODING_UTF8, cut, sizeof(cut), output,
                                   sizeof(output)) == 2U);
    assert(strcmp(output, "М") == 0);
}

static void test_nothing_to_decode_still_terminates(void)
{
    char output[4] = {'x', 'x', 'x', 'x'};
    assert(audio_tags_text_to_utf8(AUDIO_TAGS_ENCODING_UTF8, NULL, 0U, output,
                                   sizeof(output)) == 0U);
    assert(output[0] == '\0');
    // No output at all must not be a write through a null pointer.
    assert(audio_tags_text_to_utf8(AUDIO_TAGS_ENCODING_UTF8, (const uint8_t *)"A", 1U, NULL,
                                   0U) == 0U);
}

static void test_have_text_ignores_a_picture_only_tag(void)
{
    audio_tags_t tags;
    audio_tags_clear(&tags);
    assert(!audio_tags_have_text(&tags));
    tags.picture_length = 4096U;
    assert(!audio_tags_have_text(&tags));
    snprintf(tags.album, sizeof(tags.album), "%s", "Море");
    assert(audio_tags_have_text(&tags));
}

int main(void)
{
    test_utf16_with_a_little_endian_mark();
    test_utf16_with_a_big_endian_mark();
    test_utf16_without_a_mark_is_read_as_little_endian();
    test_declared_big_endian_needs_no_mark();
    test_a_surrogate_pair_becomes_one_character();
    test_a_lone_surrogate_is_dropped();
    test_windows_1251_pretending_to_be_latin1();
    test_a_single_accent_stays_latin1();
    test_a_high_byte_outside_the_cyrillic_block_settles_it();
    test_a_field_ends_at_its_first_zero();
    test_control_characters_become_spaces();
    test_trailing_blanks_are_trimmed();
    test_truncation_stops_on_a_character_boundary();
    test_utf8_input_is_bounded_and_checked();
    test_nothing_to_decode_still_terminates();
    test_have_text_ignores_a_picture_only_tag();
    printf("audio_tags_text tests passed\n");
    return 0;
}
