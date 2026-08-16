#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "settings_csv.h"

static const char *PATH = "/tmp/jradio_settings_csv_test.csv";

static void write_file(const char *contents)
{
    FILE *file = fopen(PATH, "w");
    assert(file != NULL);
    assert(fputs(contents, file) >= 0);
    assert(fclose(file) == 0);
}

static bool temp_file_exists(void)
{
    char temp[512];
    snprintf(temp, sizeof(temp), "%s.tmp", PATH);
    FILE *file = fopen(temp, "r");
    if (file == NULL) return false;
    fclose(file);
    return true;
}

static void test_replacing_a_key_keeps_the_others(void)
{
    /* The file is shared between the player (last station) and the settings
     * screen, so a write must never be a whole-file replacement. */
    char value[64] = {0};
    write_file("volume,80\nlast_station_url,http://old\nlast_station_url,http://duplicate\n");

    assert(settings_csv_set(PATH, "last_station_url", "http://new"));
    assert(settings_csv_get(PATH, "last_station_url", value, sizeof(value)));
    assert(strcmp(value, "http://new") == 0);
    assert(settings_csv_get(PATH, "volume", value, sizeof(value)));
    assert(strcmp(value, "80") == 0);

    assert(settings_csv_set(PATH, "brightness", "70"));
    assert(settings_csv_get(PATH, "brightness", value, sizeof(value)));
    assert(strcmp(value, "70") == 0);
    /* Adding a key must not disturb what was already there. */
    assert(settings_csv_get(PATH, "volume", value, sizeof(value)));
    assert(strcmp(value, "80") == 0);
    assert(settings_csv_get(PATH, "last_station_url", value, sizeof(value)));
    assert(strcmp(value, "http://new") == 0);
}

static void test_keys_from_both_writers_coexist(void)
{
    /* Exactly the interleaving that motivated the lock: the player and the
     * settings screen each own keys in one file, and neither may drop the
     * other's. */
    char value[64] = {0};
    assert(remove(PATH) == 0);

    assert(settings_csv_set(PATH, "last_station_url", "http://radio"));
    assert(settings_csv_set(PATH, "language", "en"));
    assert(settings_csv_set(PATH, "home_screen", "feed"));
    assert(settings_csv_set(PATH, "last_station_url", "http://other"));

    assert(settings_csv_get(PATH, "language", value, sizeof(value)));
    assert(strcmp(value, "en") == 0);
    assert(settings_csv_get(PATH, "home_screen", value, sizeof(value)));
    assert(strcmp(value, "feed") == 0);
    assert(settings_csv_get(PATH, "last_station_url", value, sizeof(value)));
    assert(strcmp(value, "http://other") == 0);
}

static void test_a_rejected_write_leaves_no_temp_file(void)
{
    /* The write goes through "<path>.tmp". Leaving one behind would wedge the
     * next write on some filesystems and waste a LittleFS block on all of
     * them. */
    write_file("volume,80\n");
    assert(!settings_csv_set(PATH, "bad,key", "value"));
    assert(!temp_file_exists());
    assert(!settings_csv_set(PATH, "bad", "line\nfeed"));
    assert(!temp_file_exists());
    assert(!settings_csv_set(PATH, "", "value"));
    assert(!temp_file_exists());
}

static void test_a_successful_write_leaves_no_temp_file(void)
{
    write_file("volume,80\n");
    assert(settings_csv_set(PATH, "volume", "50"));
    assert(!temp_file_exists());
    assert(settings_csv_set(PATH, "fresh", "1"));
    assert(!temp_file_exists());
}

static void test_writing_to_a_missing_file_creates_it(void)
{
    /* First boot has no settings.csv at all; that is not an error. */
    char value[64] = {0};
    (void)remove(PATH);
    assert(settings_csv_set(PATH, "language", "ru"));
    assert(settings_csv_get(PATH, "language", value, sizeof(value)));
    assert(strcmp(value, "ru") == 0);
    assert(!temp_file_exists());
}

static void test_a_value_too_long_for_the_caller_is_refused(void)
{
    char small[4] = {0};
    write_file("key,longvalue\n");
    assert(!settings_csv_get(PATH, "key", small, sizeof(small)));
    /* Missing keys and files report failure rather than a stale value. */
    assert(!settings_csv_get(PATH, "absent", small, sizeof(small)));
    assert(!settings_csv_get("/tmp/jradio_no_such_file.csv", "key", small, sizeof(small)));
}

int main(void)
{
    settings_csv_init();
    test_replacing_a_key_keeps_the_others();
    test_keys_from_both_writers_coexist();
    test_a_rejected_write_leaves_no_temp_file();
    test_a_successful_write_leaves_no_temp_file();
    test_writing_to_a_missing_file_creates_it();
    test_a_value_too_long_for_the_caller_is_refused();
    assert(remove(PATH) == 0);
    puts("settings_csv tests passed");
    return 0;
}
