#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "station_levels.h"

static const char *k_path = "/tmp/jradio_levels_test.csv";
// Large: the table is 1.5 KB, more than a test's stack wants to carry twice.
static station_levels_t s_levels;
static station_levels_t s_reloaded;

static void test_the_key_is_fnv1a(void)
{
    assert(station_levels_key("") == 2166136261U);
    assert(station_levels_key("a") == 0xe40c292cU);
    assert(station_levels_key("http://a/1") != station_levels_key("http://a/2"));
}

static void test_noted_levels_come_back_from_the_file(void)
{
    remove(k_path);
    station_levels_load(&s_levels, k_path);
    assert(s_levels.count == 0U);
    const uint32_t loud = station_levels_key("http://loud.example/stream");
    const uint32_t quiet = station_levels_key("http://quiet.example/stream");
    assert(station_levels_note(&s_levels, loud, -6.24f));
    assert(station_levels_note(&s_levels, quiet, 3.0f));
    assert(station_levels_save(&s_levels, k_path));
    station_levels_load(&s_reloaded, k_path);
    assert(s_reloaded.count == 2U);
    float db = 0.0f;
    assert(station_levels_find(&s_reloaded, loud, &db) && fabsf(db + 6.2f) < 0.001f);
    assert(station_levels_find(&s_reloaded, quiet, &db) && fabsf(db - 3.0f) < 0.001f);
    assert(!station_levels_find(&s_reloaded, 12345U, &db));
    // The one noted last is first.
    assert(s_reloaded.entries[0].key == quiet);
    remove(k_path);
}

static void test_a_small_move_is_not_written(void)
{
    s_levels.count = 0U;
    const uint32_t key = 7U;
    assert(station_levels_note(&s_levels, key, -4.0f));
    assert(!station_levels_note(&s_levels, key, -4.3f));
    float db = 0.0f;
    assert(station_levels_find(&s_levels, key, &db) && fabsf(db + 4.0f) < 0.001f);
    assert(station_levels_note(&s_levels, key, -4.6f));
    assert(station_levels_find(&s_levels, key, &db) && fabsf(db + 4.6f) < 0.001f);
    assert(s_levels.count == 1U);
}

static void test_past_the_cap_the_oldest_goes(void)
{
    s_levels.count = 0U;
    for (uint32_t key = 1U; key <= STATION_LEVELS_MAX; ++key) {
        assert(station_levels_note(&s_levels, key, 1.0f));
    }
    assert(s_levels.count == STATION_LEVELS_MAX);
    // Played again, key 1 is no longer the oldest; key 2 is.
    (void)station_levels_note(&s_levels, 1U, 1.0f);
    assert(station_levels_note(&s_levels, 9999U, 2.0f));
    assert(s_levels.count == STATION_LEVELS_MAX);
    assert(station_levels_find(&s_levels, 1U, NULL));
    assert(!station_levels_find(&s_levels, 2U, NULL));
    assert(station_levels_find(&s_levels, 9999U, NULL));
    // And the whole table fits the backup's 8 KB member.
    assert(station_levels_save(&s_levels, k_path));
    FILE *file = fopen(k_path, "rb");
    assert(file != NULL);
    assert(fseek(file, 0, SEEK_END) == 0);
    assert(ftell(file) < 8192);
    fclose(file);
    remove(k_path);
}

static void test_bad_lines_are_skipped(void)
{
    FILE *file = fopen(k_path, "w");
    assert(file != NULL);
    fputs("#lufs\n"
          "0000000a,-35\n"
          "garbage\n"
          "zz,1\n"
          "0000000b,abc\n"
          "0000000c,99999\n"
          "000000d,5\n"
          "0000000a,40\n"
          "0000000e,12\r\n",
          file);
    for (int i = 0; i < 100; ++i) fputc('x', file);
    fputs("\n0000000f,-1\n", file);
    fclose(file);
    station_levels_load(&s_levels, k_path);
    assert(s_levels.count == 3U);
    float db = 0.0f;
    assert(station_levels_find(&s_levels, 0xaU, &db) && fabsf(db + 3.5f) < 0.001f);
    assert(station_levels_find(&s_levels, 0xeU, &db) && fabsf(db - 1.2f) < 0.001f);
    assert(station_levels_find(&s_levels, 0xfU, &db) && fabsf(db + 0.1f) < 0.001f);
    remove(k_path);
}

static void test_a_nudge_rides_beside_the_measurement(void)
{
    s_levels.count = 0U;
    remove(k_path);
    // Nothing to say for a station unknown and left alone.
    assert(!station_levels_set_nudge(&s_levels, 1U, 0));
    assert(s_levels.count == 0U);
    // Nudged before it was ever measured: a nudge and no level.
    assert(station_levels_set_nudge(&s_levels, 1U, 3));
    assert(!station_levels_set_nudge(&s_levels, 1U, 3));
    assert(!station_levels_find(&s_levels, 1U, NULL));
    assert(station_levels_nudge(&s_levels, 1U) == 3);
    // Its first measurement is written even though it moved "nothing".
    assert(station_levels_note(&s_levels, 1U, 0.1f));
    float db = 0.0f;
    assert(station_levels_find(&s_levels, 1U, &db) && fabsf(db - 0.1f) < 0.001f);
    // A measured station keeps its nudge through a remeasurement, small or not.
    assert(!station_levels_note(&s_levels, 1U, 0.3f));
    assert(station_levels_note(&s_levels, 1U, -2.0f));
    assert(station_levels_nudge(&s_levels, 1U) == 3);
    // And the measurement through a nudge.
    assert(station_levels_note(&s_levels, 2U, -5.0f));
    assert(station_levels_set_nudge(&s_levels, 2U, -2));
    assert(station_levels_note(&s_levels, 3U, 4.0f));
    assert(station_levels_set_nudge(&s_levels, 4U, -6));
    assert(station_levels_save(&s_levels, k_path));
    FILE *file = fopen(k_path, "r");
    assert(file != NULL);
    char text[128];
    const size_t length = fread(text, 1U, sizeof(text) - 1U, file);
    text[length] = '\0';
    fclose(file);
    // No third field without a nudge, no second without a measurement.
    assert(strcmp(text, "#lufs\n00000004,,-6\n00000003,40\n00000002,-50,-2\n00000001,-20,3\n") == 0);
    station_levels_load(&s_reloaded, k_path);
    assert(s_reloaded.count == 4U);
    assert(station_levels_find(&s_reloaded, 2U, &db) && fabsf(db + 5.0f) < 0.001f);
    assert(station_levels_nudge(&s_reloaded, 2U) == -2);
    assert(station_levels_nudge(&s_reloaded, 3U) == 0);
    assert(!station_levels_find(&s_reloaded, 4U, NULL));
    assert(station_levels_nudge(&s_reloaded, 4U) == -6);
    assert(station_levels_nudge(&s_reloaded, 99U) == 0);
    remove(k_path);
}

static void test_bad_nudges_are_skipped(void)
{
    FILE *file = fopen(k_path, "w");
    assert(file != NULL);
    fputs("#lufs\n"
          "0000000a,,\n"
          "0000000b,\n"
          "0000000c,5,x\n"
          "0000000d,5,99\n"
          "0000000e,5,1,2\n"
          "0000000f,,0\n"
          "00000010,-5,-2\n",
          file);
    fclose(file);
    station_levels_load(&s_levels, k_path);
    assert(s_levels.count == 1U);
    assert(station_levels_nudge(&s_levels, 0x10U) == -2);
    remove(k_path);
}

/* A file from before the loudness was kept holds corrections, which mean
   nothing against another target: read as empty, and written anew. */
static void test_a_file_of_corrections_is_read_as_empty(void)
{
    FILE *file = fopen(k_path, "w");
    assert(file != NULL);
    fputs("0000000a,-35\n0000000b,40\n", file);
    fclose(file);
    station_levels_load(&s_levels, k_path);
    assert(s_levels.count == 0U);
    file = fopen(k_path, "w");
    assert(file != NULL);
    fclose(file);
    station_levels_load(&s_levels, k_path);
    assert(s_levels.count == 0U);
    remove(k_path);
}

int main(void)
{
    test_the_key_is_fnv1a();
    test_noted_levels_come_back_from_the_file();
    test_a_small_move_is_not_written();
    test_past_the_cap_the_oldest_goes();
    test_bad_lines_are_skipped();
    test_a_nudge_rides_beside_the_measurement();
    test_bad_nudges_are_skipped();
    test_a_file_of_corrections_is_read_as_empty();
    puts("station levels tests passed");
    return 0;
}
