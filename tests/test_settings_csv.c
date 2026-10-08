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

/* The snapshot answers every key exactly as get() does - first match wins,
 * a line too long for get()'s buffer is cut where get() cuts it, a value too
 * long for the caller is refused - and reads the file once. */
static void expect_same(const settings_csv_snapshot_t *snapshot, const char *key,
                        size_t value_size)
{
    char direct[300] = {0};
    char snapped[300] = {0};
    const bool from_file = settings_csv_get(PATH, key, direct, value_size);
    const bool from_snapshot = settings_csv_snapshot_get(snapshot, key, snapped, value_size);
    assert(from_file == from_snapshot);
    if (from_file) assert(strcmp(direct, snapped) == 0);
}

static void test_a_snapshot_reads_like_get(void)
{
    char contents[2048];
    char long_value[700];
    memset(long_value, 'x', sizeof(long_value) - 1U);
    long_value[sizeof(long_value) - 1U] = '\0';
    snprintf(contents, sizeof(contents),
             "language,en\n"
             "volume,42\r\n"
             "volume,99\n"
             "broken line\n"
             ",novalue\n"
             "long,%s\n"
             "tail,end",
             long_value);
    write_file(contents);
    settings_csv_snapshot_t snapshot;
    settings_csv_snapshot_load(&snapshot, PATH);
    assert(snapshot.text != NULL);
    const char *keys[] = {"language", "volume", "broken line", "long", "tail", "absent", "x"};
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        expect_same(&snapshot, keys[i], 300U);
        expect_same(&snapshot, keys[i], 3U);
    }
    char value[16];
    assert(settings_csv_snapshot_get(&snapshot, "volume", value, sizeof(value)));
    assert(strcmp(value, "42") == 0);
    assert(settings_csv_snapshot_get(&snapshot, "tail", value, sizeof(value)));
    assert(strcmp(value, "end") == 0);
    /* Read once: a write after the load is not seen until the next one. */
    assert(settings_csv_set(PATH, "language", "ru"));
    assert(settings_csv_snapshot_get(&snapshot, "language", value, sizeof(value)));
    assert(strcmp(value, "en") == 0);
    settings_csv_snapshot_free(&snapshot);
    assert(snapshot.text == NULL);
}

static void test_a_snapshot_of_no_file_is_empty(void)
{
    settings_csv_snapshot_t snapshot;
    settings_csv_snapshot_load(&snapshot, "/tmp/jradio_no_such_file.csv");
    assert(snapshot.text != NULL);
    char value[8];
    assert(!settings_csv_snapshot_get(&snapshot, "language", value, sizeof(value)));
    settings_csv_snapshot_free(&snapshot);
}

static void test_a_file_too_big_to_hold_is_still_read(void)
{
    FILE *file = fopen(PATH, "w");
    assert(file != NULL);
    assert(fputs("first,1\n", file) >= 0);
    for (unsigned i = 0; i * 32U <= SETTINGS_CSV_SNAPSHOT_MAX; ++i) {
        assert(fprintf(file, "filler%05u,%024u\n", i, i) > 0);
    }
    assert(fputs("last,2\n", file) >= 0);
    assert(fclose(file) == 0);
    settings_csv_snapshot_t snapshot;
    settings_csv_snapshot_load(&snapshot, PATH);
    assert(snapshot.text == NULL);
    char value[8];
    assert(settings_csv_snapshot_get(&snapshot, "first", value, sizeof(value)));
    assert(strcmp(value, "1") == 0);
    assert(settings_csv_snapshot_get(&snapshot, "last", value, sizeof(value)));
    assert(strcmp(value, "2") == 0);
    settings_csv_snapshot_free(&snapshot);
}

static int file_has(const char *line)
{
    char text[1024];
    FILE *file = fopen(PATH, "r");
    if (file == NULL) return 0;
    const size_t length = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[length] = '\0';
    return strstr(text, line) != NULL;
}

/* The screen saves the volume in the background; until the write is on the
 * card the value is answered from the queue, so a reload of the settings in
 * that moment does not put the old volume back. */
static void test_a_later_write_answers_before_it_is_on_the_card(void)
{
    write_file("volume,30\nlast_station_url,http://a/\n");
    assert(settings_csv_set_later(PATH, "volume", "45"));
    assert(file_has("volume,30\n"));
    char value[16];
    assert(settings_csv_get(PATH, "volume", value, sizeof(value)));
    assert(strcmp(value, "45") == 0);
    settings_csv_snapshot_t snapshot;
    settings_csv_snapshot_load(&snapshot, PATH);
    assert(settings_csv_snapshot_get(&snapshot, "volume", value, sizeof(value)));
    assert(strcmp(value, "45") == 0);
    /* The other keys still come from the file. */
    assert(settings_csv_snapshot_get(&snapshot, "last_station_url", value, sizeof(value)));
    settings_csv_snapshot_free(&snapshot);

    /* A second turn before the write replaces the first, and one write
     * carries both keys. */
    assert(settings_csv_set_later(PATH, "volume", "50"));
    assert(settings_csv_set_later(PATH, "brightness", "70"));
    assert(settings_csv_flush_pending() == 2U);
    assert(file_has("volume,50\n"));
    assert(file_has("brightness,70\n"));
    assert(file_has("last_station_url,http://a/\n"));
    assert(settings_csv_flush_pending() == 0U);
    /* Written: answered by the file again. */
    assert(settings_csv_get(PATH, "volume", value, sizeof(value)));
    assert(strcmp(value, "50") == 0);
}

static void test_what_the_queue_cannot_hold_is_written_at_once(void)
{
    write_file("");
    char long_value[64];
    memset(long_value, 'x', sizeof(long_value) - 1);
    long_value[sizeof(long_value) - 1] = '\0';
    assert(settings_csv_set_later(PATH, "device_name", long_value));
    assert(file_has(long_value));
    assert(settings_csv_flush_pending() == 0U);
    /* And what set() refuses, set_later() refuses too. */
    assert(!settings_csv_set_later(PATH, "volume", "a,b"));
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
    test_a_snapshot_reads_like_get();
    test_a_snapshot_of_no_file_is_empty();
    test_a_file_too_big_to_hold_is_still_read();
    test_a_later_write_answers_before_it_is_on_the_card();
    test_what_the_queue_cannot_hold_is_written_at_once();
    assert(remove(PATH) == 0);
    puts("settings_csv tests passed");
    return 0;
}
