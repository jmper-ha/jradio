#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config_archive.h"

static const char WIFI_JSON[] =
    "{\"version\":1,\"networks\":[{\"ssid\":\"home\",\"password\":\"secret\"}]}";
static const char SETTINGS_CSV[] = "language,ru\nbrightness,45\nlast_station_url,http://a\n";
static const char YANDEX_JSON[] = "{\"version\":1,\"token\":\"abc\",\"device_id\":\"d1\"}";

static void test_member_names_map_both_ways(void)
{
    assert(strcmp(config_archive_member_file(CONFIG_ARCHIVE_MEMBER_WIFI), "wifi.json") == 0);
    assert(strcmp(config_archive_member_file(CONFIG_ARCHIVE_MEMBER_SETTINGS), "settings.csv") == 0);
    assert(strcmp(config_archive_member_file(CONFIG_ARCHIVE_MEMBER_YANDEX), "yandex.json") == 0);
    assert(config_archive_member_file(CONFIG_ARCHIVE_MEMBER_UNKNOWN) == NULL);

    assert(config_archive_member_from_file("wifi.json") == CONFIG_ARCHIVE_MEMBER_WIFI);
    /* A zip made by a file manager out of the config directory keeps the
     * folder, and Windows may hand the name back in another case. */
    assert(config_archive_member_from_file("config/settings.csv") ==
           CONFIG_ARCHIVE_MEMBER_SETTINGS);
    assert(config_archive_member_from_file("littlefs\\config\\Yandex.JSON") ==
           CONFIG_ARCHIVE_MEMBER_YANDEX);
    assert(config_archive_member_from_file("stations.csv") == CONFIG_ARCHIVE_MEMBER_UNKNOWN);
    assert(config_archive_member_from_file("wifi.json.bak") == CONFIG_ARCHIVE_MEMBER_UNKNOWN);
    assert(config_archive_member_from_file("config/") == CONFIG_ARCHIVE_MEMBER_UNKNOWN);
    assert(config_archive_member_from_file(NULL) == CONFIG_ARCHIVE_MEMBER_UNKNOWN);
}

static void test_plausibility_gates_the_obvious_wrong_file(void)
{
    assert(config_archive_member_is_plausible(CONFIG_ARCHIVE_MEMBER_WIFI, WIFI_JSON,
                                              sizeof(WIFI_JSON) - 1U));
    assert(config_archive_member_is_plausible(CONFIG_ARCHIVE_MEMBER_YANDEX, YANDEX_JSON,
                                              sizeof(YANDEX_JSON) - 1U));
    assert(config_archive_member_is_plausible(CONFIG_ARCHIVE_MEMBER_SETTINGS, SETTINGS_CSV,
                                              sizeof(SETTINGS_CSV) - 1U));
    /* Leading and trailing whitespace is what a hand-edited file has. */
    static const char padded[] = "\n  {\"version\":1,\"networks\":[]}\n";
    assert(config_archive_member_is_plausible(CONFIG_ARCHIVE_MEMBER_WIFI, padded,
                                              sizeof(padded) - 1U));

    /* A picture under the right name is the case this gate exists for. */
    static const unsigned char png[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    assert(!config_archive_member_is_plausible(CONFIG_ARCHIVE_MEMBER_WIFI, png, sizeof(png)));
    assert(!config_archive_member_is_plausible(CONFIG_ARCHIVE_MEMBER_SETTINGS, png, sizeof(png)));
    /* JSON, but somebody else's: the loader would read it as no networks at
     * all and the device would come up on the setup access point. */
    static const char foreign[] = "{\"networks\":[]}";
    assert(!config_archive_member_is_plausible(CONFIG_ARCHIVE_MEMBER_WIFI, foreign,
                                              sizeof(foreign) - 1U));
    /* The catalogue is comma-separated text too, so the shape check cannot
     * tell it from settings.csv - but a file with no comma at all is not a
     * settings file under any reading. */
    static const char no_comma[] = "just some words\n";
    assert(!config_archive_member_is_plausible(CONFIG_ARCHIVE_MEMBER_SETTINGS, no_comma,
                                              sizeof(no_comma) - 1U));
    assert(!config_archive_member_is_plausible(CONFIG_ARCHIVE_MEMBER_SETTINGS, "", 0U));
    assert(!config_archive_member_is_plausible(CONFIG_ARCHIVE_MEMBER_UNKNOWN, WIFI_JSON,
                                              sizeof(WIFI_JSON) - 1U));
    assert(!config_archive_member_is_plausible(CONFIG_ARCHIVE_MEMBER_WIFI, NULL, 4U));
    /* A path off the drive is UTF-8 and has to survive the round trip. */
    static const char cyrillic[] = "last_usb_file,/usb0/Музыка/трек.mp3\n";
    assert(config_archive_member_is_plausible(CONFIG_ARCHIVE_MEMBER_SETTINGS, cyrillic,
                                              sizeof(cyrillic) - 1U));
}

static void test_crc32_matches_the_known_answer(void)
{
    /* The IEEE check value every zip tool agrees on. */
    assert(config_archive_crc32("123456789", 9U) == 0xCBF43926UL);
    assert(config_archive_crc32("", 0U) == 0UL);
}

static void test_dos_stamp_refuses_a_time_the_device_does_not_have(void)
{
    assert(config_archive_dos_date(2026, 9, 8) == (uint16_t)(((2026 - 1980) << 9) | (9 << 5) | 8));
    assert(config_archive_dos_time(14, 30, 20) == (uint16_t)((14 << 11) | (30 << 5) | 10));
    /* Before SNTP has answered the year is 1970, which these fields cannot
     * express at all; 1980 is the floor readers accept. */
    assert(config_archive_dos_date(1970, 1, 1) == CONFIG_ARCHIVE_DOS_DATE_MIN);
    assert(config_archive_dos_date(2026, 13, 1) == CONFIG_ARCHIVE_DOS_DATE_MIN);
    assert(config_archive_dos_time(24, 0, 0) == 0U);
}

static size_t build_full_archive(uint8_t *buffer, size_t capacity)
{
    config_archive_writer_t writer;
    config_archive_writer_init(&writer, buffer, capacity, config_archive_dos_date(2026, 9, 8),
                               config_archive_dos_time(12, 0, 0));
    assert(config_archive_writer_add(&writer, "wifi.json", WIFI_JSON, sizeof(WIFI_JSON) - 1U));
    assert(config_archive_writer_add(&writer, "settings.csv", SETTINGS_CSV,
                                     sizeof(SETTINGS_CSV) - 1U));
    assert(config_archive_writer_add(&writer, "yandex.json", YANDEX_JSON,
                                     sizeof(YANDEX_JSON) - 1U));
    size_t length = 0U;
    assert(config_archive_writer_finish(&writer, &length));
    return length;
}

static void test_round_trip_returns_every_member_unchanged(void)
{
    uint8_t buffer[CONFIG_ARCHIVE_CAPACITY(sizeof(WIFI_JSON) + sizeof(SETTINGS_CSV) +
                                           sizeof(YANDEX_JSON))];
    const size_t length = build_full_archive(buffer, sizeof(buffer));
    assert(config_archive_looks_like_zip(buffer, length));

    config_archive_reader_t reader;
    config_archive_reader_init(&reader, buffer, length);
    const char *expected_names[] = {"wifi.json", "settings.csv", "yandex.json"};
    const char *expected_data[] = {WIFI_JSON, SETTINGS_CSV, YANDEX_JSON};
    const size_t expected_sizes[] = {sizeof(WIFI_JSON) - 1U, sizeof(SETTINGS_CSV) - 1U,
                                     sizeof(YANDEX_JSON) - 1U};
    for (size_t index = 0U; index < 3U; ++index) {
        config_archive_entry_t entry;
        assert(config_archive_reader_next(&reader, &entry) == CONFIG_ARCHIVE_READ_OK);
        assert(strcmp(entry.name, expected_names[index]) == 0);
        assert(!entry.deflated);
        assert(entry.size == expected_sizes[index]);
        assert(entry.original_size == entry.size);
        assert(entry.crc == config_archive_crc32(expected_data[index], entry.size));
        assert(memcmp(entry.data, expected_data[index], entry.size) == 0);
        assert(config_archive_member_from_file(entry.name) != CONFIG_ARCHIVE_MEMBER_UNKNOWN);
        assert(config_archive_member_is_plausible(config_archive_member_from_file(entry.name),
                                                  entry.data, entry.size));
    }
    config_archive_entry_t entry;
    assert(config_archive_reader_next(&reader, &entry) == CONFIG_ARCHIVE_READ_END);
    assert(config_archive_reader_next(&reader, &entry) == CONFIG_ARCHIVE_READ_END);
}

static void test_archive_carries_a_readable_central_directory(void)
{
    uint8_t buffer[CONFIG_ARCHIVE_CAPACITY(sizeof(WIFI_JSON) + sizeof(SETTINGS_CSV) +
                                           sizeof(YANDEX_JSON))];
    const size_t length = build_full_archive(buffer, sizeof(buffer));
    /* The end record is what a zip tool looks for first: without it the file
     * downloads and then refuses to open, which is the failure that is only
     * noticed on the day the backup is needed. */
    const uint8_t *end = buffer + length - 22U;
    assert(memcmp(end, "PK\x05\x06", 4U) == 0);
    assert(end[8] == 3U && end[9] == 0U);
    const uint32_t directory_size = (uint32_t)end[12] | ((uint32_t)end[13] << 8) |
                                    ((uint32_t)end[14] << 16) | ((uint32_t)end[15] << 24);
    const uint32_t directory_offset = (uint32_t)end[16] | ((uint32_t)end[17] << 8) |
                                      ((uint32_t)end[18] << 16) | ((uint32_t)end[19] << 24);
    assert(directory_offset + directory_size + 22U == length);
    assert(memcmp(buffer + directory_offset, "PK\x01\x02", 4U) == 0);
}

static void test_writer_refuses_rather_than_truncates(void)
{
    /* One byte short of the payload: the failure has to survive to finish(),
     * because a shorter archive that still parses is a backup missing a file
     * nobody would notice. */
    uint8_t buffer[30U + sizeof("wifi.json") - 1U + sizeof(WIFI_JSON) - 1U + 40U];
    config_archive_writer_t writer;
    config_archive_writer_init(&writer, buffer, sizeof(buffer), CONFIG_ARCHIVE_DOS_DATE_MIN, 0U);
    assert(config_archive_writer_add(&writer, "wifi.json", WIFI_JSON, sizeof(WIFI_JSON) - 1U));
    assert(!config_archive_writer_add(&writer, "settings.csv", SETTINGS_CSV,
                                      sizeof(SETTINGS_CSV) - 1U));
    size_t length = 0U;
    assert(!config_archive_writer_finish(&writer, &length));
    assert(length == 0U);

    config_archive_writer_t nowhere;
    config_archive_writer_init(&nowhere, NULL, 64U, CONFIG_ARCHIVE_DOS_DATE_MIN, 0U);
    assert(!config_archive_writer_add(&nowhere, "wifi.json", WIFI_JSON, 4U));
    assert(!config_archive_writer_finish(&nowhere, NULL));

    /* More members than the device has files. */
    uint8_t roomy[512];
    config_archive_writer_t full;
    config_archive_writer_init(&full, roomy, sizeof(roomy), CONFIG_ARCHIVE_DOS_DATE_MIN, 0U);
    for (size_t index = 0U; index < CONFIG_ARCHIVE_MEMBER_MAX; ++index) {
        assert(config_archive_writer_add(&full, "settings.csv", "a,b\n", 4U));
    }
    assert(!config_archive_writer_add(&full, "settings.csv", "a,b\n", 4U));
}

static void test_reader_rejects_what_it_cannot_unpack(void)
{
    uint8_t buffer[CONFIG_ARCHIVE_CAPACITY(sizeof(WIFI_JSON))];
    config_archive_writer_t writer;
    config_archive_writer_init(&writer, buffer, sizeof(buffer), CONFIG_ARCHIVE_DOS_DATE_MIN, 0U);
    assert(config_archive_writer_add(&writer, "wifi.json", WIFI_JSON, sizeof(WIFI_JSON) - 1U));
    size_t length = 0U;
    assert(config_archive_writer_finish(&writer, &length));

    config_archive_reader_t reader;
    config_archive_entry_t entry;

    /* Not an archive at all. */
    assert(!config_archive_looks_like_zip("hello", 5U));
    assert(!config_archive_looks_like_zip(NULL, 8U));
    assert(!config_archive_looks_like_zip(buffer, 2U));
    config_archive_reader_init(&reader, "hello there", 11U);
    assert(config_archive_reader_next(&reader, &entry) == CONFIG_ARCHIVE_READ_MALFORMED);

    /* Packed with something neither stored nor deflate - an archive this
     * device cannot open, told apart from one that is damaged. */
    uint8_t bzipped[sizeof(buffer)];
    memcpy(bzipped, buffer, length);
    bzipped[8] = 12U;
    config_archive_reader_init(&reader, bzipped, length);
    assert(config_archive_reader_next(&reader, &entry) == CONFIG_ARCHIVE_READ_COMPRESSED);

    /* Sizes only in a trailing descriptor - the local header lies. */
    uint8_t streamed[sizeof(buffer)];
    memcpy(streamed, buffer, length);
    streamed[6] = 0x08U;
    config_archive_reader_init(&reader, streamed, length);
    assert(config_archive_reader_next(&reader, &entry) == CONFIG_ARCHIVE_READ_MALFORMED);

    /* Deflated - what Explorer and Finder produce out of whatever they
       repack. The reader hands it over with both sizes and the file's own CRC
       and leaves the inflating to the caller; it must not check the CRC of
       bytes that are still compressed. */
    uint8_t deflated[sizeof(buffer)];
    memcpy(deflated, buffer, length);
    deflated[8] = 8U;
    config_archive_reader_init(&reader, deflated, length);
    assert(config_archive_reader_next(&reader, &entry) == CONFIG_ARCHIVE_READ_OK);
    assert(entry.deflated);
    assert(strcmp(entry.name, "wifi.json") == 0);
    assert(entry.original_size == sizeof(WIFI_JSON) - 1U);
    assert(entry.crc == config_archive_crc32(WIFI_JSON, sizeof(WIFI_JSON) - 1U));

    /* A byte flipped in the payload: caught before it reaches the flash. */
    uint8_t damaged[sizeof(buffer)];
    memcpy(damaged, buffer, length);
    damaged[30U + 9U + 3U] ^= 0x20U;
    config_archive_reader_init(&reader, damaged, length);
    assert(config_archive_reader_next(&reader, &entry) == CONFIG_ARCHIVE_READ_CRC);

    /* Cut off mid-payload, the shape a half-finished upload has. */
    config_archive_reader_init(&reader, buffer, 34U);
    assert(config_archive_reader_next(&reader, &entry) == CONFIG_ARCHIVE_READ_MALFORMED);
    config_archive_reader_init(&reader, buffer, 12U);
    assert(config_archive_reader_next(&reader, &entry) == CONFIG_ARCHIVE_READ_MALFORMED);

    config_archive_reader_init(&reader, NULL, 0U);
    assert(config_archive_reader_next(&reader, &entry) == CONFIG_ARCHIVE_READ_MALFORMED);
}

static void test_foreign_entries_are_walked_over_not_refused(void)
{
    /* Somebody's backup of the whole config directory: our three files plus
     * whatever else was in it. Restoring has to find ours and step past the
     * rest, including a name too long to be one of ours. */
    static const char long_name[] =
        "some/very/deeply/nested/directory/that/nobody/expected/file.txt";
    uint8_t buffer[512];
    config_archive_writer_t writer;
    /* Written by hand, because the writer refuses a name this long - it is
     * only ever asked to write ours. */
    config_archive_entry_t entry;
    const size_t name_length = sizeof(long_name) - 1U;
    const char payload[] = "not ours";
    size_t offset = 0U;
    memcpy(buffer + offset, "PK\x03\x04", 4U);
    memset(buffer + offset + 4U, 0, 26U);
    buffer[offset + 4U] = 20U;
    const uint32_t crc = config_archive_crc32(payload, sizeof(payload) - 1U);
    for (int shift = 0; shift < 32; shift += 8) {
        buffer[offset + 14U + (size_t)(shift / 8)] = (uint8_t)((crc >> shift) & 0xFFU);
        buffer[offset + 18U + (size_t)(shift / 8)] =
            (uint8_t)(((sizeof(payload) - 1U) >> shift) & 0xFFU);
        buffer[offset + 22U + (size_t)(shift / 8)] =
            (uint8_t)(((sizeof(payload) - 1U) >> shift) & 0xFFU);
    }
    buffer[offset + 26U] = (uint8_t)(name_length & 0xFFU);
    buffer[offset + 27U] = (uint8_t)(name_length >> 8);
    memcpy(buffer + offset + 30U, long_name, name_length);
    memcpy(buffer + offset + 30U + name_length, payload, sizeof(payload) - 1U);
    offset += 30U + name_length + sizeof(payload) - 1U;

    config_archive_writer_init(&writer, buffer + offset, sizeof(buffer) - offset,
                               CONFIG_ARCHIVE_DOS_DATE_MIN, 0U);
    assert(config_archive_writer_add(&writer, "config/wifi.json", WIFI_JSON,
                                     sizeof(WIFI_JSON) - 1U));
    size_t tail = 0U;
    assert(config_archive_writer_finish(&writer, &tail));

    config_archive_reader_t reader;
    config_archive_reader_init(&reader, buffer, offset + tail);
    assert(config_archive_reader_next(&reader, &entry) == CONFIG_ARCHIVE_READ_OK);
    assert(entry.name[0] == '\0');
    assert(config_archive_member_from_file(entry.name) == CONFIG_ARCHIVE_MEMBER_UNKNOWN);
    assert(config_archive_reader_next(&reader, &entry) == CONFIG_ARCHIVE_READ_OK);
    assert(config_archive_member_from_file(entry.name) == CONFIG_ARCHIVE_MEMBER_WIFI);
    assert(config_archive_reader_next(&reader, &entry) == CONFIG_ARCHIVE_READ_END);
}

int main(void)
{
    test_member_names_map_both_ways();
    test_plausibility_gates_the_obvious_wrong_file();
    test_crc32_matches_the_known_answer();
    test_dos_stamp_refuses_a_time_the_device_does_not_have();
    test_round_trip_returns_every_member_unchanged();
    test_archive_carries_a_readable_central_directory();
    test_writer_refuses_rather_than_truncates();
    test_reader_rejects_what_it_cannot_unpack();
    test_foreign_entries_are_walked_over_not_refused();
    printf("config_archive tests passed\n");
    return 0;
}
