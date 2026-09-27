#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "fm_presets.h"

static fm_presets_t presets;

static size_t parse(const char *text)
{
    return fm_presets_parse(text, strlen(text), &presets);
}

static void test_a_file_reads_in_its_order(void)
{
    assert(parse("Радио Шоколад\t98800\ts71b6a7ac.png\n"
                 "# a comment\n"
                 "\n"
                 "Европа Плюс\t106200\r\n") == 0U);
    assert(presets.count == 2U);
    assert(strcmp(presets.presets[0].name, "Радио Шоколад") == 0);
    assert(presets.presets[0].khz == 98800U);
    assert(strcmp(presets.presets[0].icon, "s71b6a7ac.png") == 0);
    assert(strcmp(presets.presets[1].name, "Европа Плюс") == 0);
    assert(presets.presets[1].khz == 106200U);
    assert(presets.presets[1].icon[0] == '\0');
    size_t index = 99U;
    assert(fm_presets_find(&presets, 106200U, &index) && index == 1U);
    assert(!fm_presets_find(&presets, 101200U, &index));
}

static void test_a_nameless_preset_is_its_frequency(void)
{
    // The scan saves what it finds before anybody names it.
    assert(parse("\t101200\n") == 0U);
    assert(presets.count == 1U);
    assert(presets.presets[0].name[0] == '\0');
    assert(presets.presets[0].khz == 101200U);
}

static void test_bad_lines_are_skipped_not_fatal(void)
{
    assert(parse("no tab here\n"
                 "Off the band\t76000\n"
                 "Not a number\t101.2\n"
                 "Out of its directory\t101200\t../wifi.json\n"
                 "Good\t88300\n") == 4U);
    assert(presets.count == 1U);
    assert(strcmp(presets.presets[0].name, "Good") == 0);
}

static void test_a_long_name_is_cut_on_a_whole_character(void)
{
    char line[256];
    char name[128] = "";
    for (int i = 0; i < 30; ++i) strcat(name, "Я");  // 60 bytes
    snprintf(line, sizeof(line), "%s\t101200\n", name);
    assert(parse(line) == 0U);
    const size_t length = strlen(presets.presets[0].name);
    assert(length < FM_PRESET_NAME_MAX_LEN);
    assert(length % 2U == 0U);
}

static void test_the_list_stops_at_its_maximum(void)
{
    char text[FM_PRESETS_TEXT_MAX_LEN];
    size_t used = 0U;
    for (int i = 0; i < FM_PRESETS_MAX + 3; ++i) {
        used += (size_t)snprintf(text + used, sizeof(text) - used, "S%d\t%d\n", i, 87500 + i * 100);
    }
    assert(parse(text) == 3U);
    assert(presets.count == FM_PRESETS_MAX);
}

static void test_writing_reads_back_the_same(void)
{
    const char *original = "Радио Шоколад\t98800\ts71b6a7ac.png\n\t101200\nEuropa\t106200\n";
    assert(parse(original) == 0U);
    char text[FM_PRESETS_TEXT_MAX_LEN];
    const size_t length = fm_presets_write(&presets, text, sizeof(text));
    assert(length == strlen(original));
    assert(strcmp(text, original) == 0);
    // Too short a buffer is cut, terminated, and says what it needed.
    char small[8];
    assert(fm_presets_write(&presets, small, sizeof(small)) == length);
    assert(strlen(small) == sizeof(small) - 1U);
}

int main(void)
{
    test_a_file_reads_in_its_order();
    test_a_nameless_preset_is_its_frequency();
    test_bad_lines_are_skipped_not_fatal();
    test_a_long_name_is_cut_on_a_whole_character();
    test_the_list_stops_at_its_maximum();
    test_writing_reads_back_the_same();
    puts("fm_presets tests passed");
    return 0;
}
