#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "station_catalog.h"

static void test_parse_valid_row(void)
{
    station_catalog_entry_t entry;

    assert(station_catalog_parse_line(
        "Radio Montmartre\thttp://montmartre.ice.infomaniak.ch/montmartre.mp3\t0\n", &entry));
    assert(strcmp(entry.name, "Radio Montmartre") == 0);
    assert(strcmp(entry.url, "http://montmartre.ice.infomaniak.ch/montmartre.mp3") == 0);
    assert(entry.flag == 0);
}

static void test_parse_crlf_row(void)
{
    station_catalog_entry_t entry;

    assert(station_catalog_parse_line(
        "Technolovers - Funky House\thttp://stream1-technolovers.radiohost.de/funky-house\t1\r\n",
        &entry));
    assert(strcmp(entry.name, "Technolovers - Funky House") == 0);
    assert(strcmp(entry.url, "http://stream1-technolovers.radiohost.de/funky-house") == 0);
    assert(entry.flag == 1);
}

static void test_reject_rows_with_extra_fields(void)
{
    station_catalog_entry_t entry;

    /* Four columns is a station with a picture; five is still a malformed
       line. */
    assert(!station_catalog_parse_line("A\tB\t0\tC\tD\n", &entry));
}

/* The fourth column names the station's picture, and it is optional: a
   playlist written before the column existed still loads. */
static void test_parse_row_with_icon(void)
{
    station_catalog_entry_t entry;

    assert(station_catalog_parse_line("A\thttp://x\t1\ts7f3a91c.png\n", &entry));
    assert(strcmp(entry.icon, "s7f3a91c.png") == 0);
    assert(entry.flag == 1);

    assert(station_catalog_parse_line("A\thttp://x\t1\n", &entry));
    assert(entry.icon[0] == '\0');

    /* An empty fourth column is simply no picture. */
    assert(station_catalog_parse_line("A\thttp://x\t0\t\n", &entry));
    assert(entry.icon[0] == '\0');
}

/* The name is joined to one directory, so anything that could reach out of it
   takes the whole line down rather than being quietly cleaned up. */
static void test_reject_icon_that_is_a_path(void)
{
    station_catalog_entry_t entry;

    assert(!station_catalog_parse_line("A\thttp://x\t0\t../secret.png\n", &entry));
    assert(!station_catalog_parse_line("A\thttp://x\t0\tsub/dir.png\n", &entry));
    assert(!station_catalog_parse_line("A\thttp://x\t0\t.hidden\n", &entry));
    assert(!station_catalog_icon_is_valid("a b.png"));
    assert(station_catalog_icon_is_valid(""));
    assert(station_catalog_icon_is_valid("s-1_2.png"));
}

static void test_reject_unsupported_flag(void)
{
    station_catalog_entry_t entry;

    assert(!station_catalog_parse_line("A\tB\t2\n", &entry));
}

static void test_skip_malformed_row(void)
{
    station_catalog_t catalog = {0};
    const char *text =
        "bad row without tabs\n"
        "Jazz Radio Lounge\thttps://jazzlounge.ice.infomaniak.ch/jazzlounge-high.mp3\t0\n"
        "missing flag\thttp://example.invalid/stream\t\n"
        "too\tmany\tfields\t0\n";

    assert(station_catalog_load_text(text, &catalog));
    assert(catalog.count == 1);
    assert(strcmp(catalog.entries[0].name, "Jazz Radio Lounge") == 0);
}

static void test_find_saved_url(void)
{
    station_catalog_t catalog = {0};
    size_t index = 0;

    assert(station_catalog_load_text(
        "Radio Montmartre\thttp://montmartre.ice.infomaniak.ch/montmartre.mp3\t0\n"
        "Jazz Radio Lounge\thttps://jazzlounge.ice.infomaniak.ch/jazzlounge-high.mp3\t0\n",
        &catalog));
    assert(station_catalog_find_by_url(
        &catalog, "https://jazzlounge.ice.infomaniak.ch/jazzlounge-high.mp3", &index));
    assert(index == 1);
}

static void test_does_not_find_absent_url(void)
{
    station_catalog_t catalog = {0};
    size_t index = 0;

    assert(station_catalog_load_text(
        "Radio Montmartre\thttp://montmartre.ice.infomaniak.ch/montmartre.mp3\t0\n", &catalog));
    assert(!station_catalog_find_by_url(&catalog, "http://missing.example/stream", &index));
}

int main(void)
{
    test_parse_valid_row();
    test_parse_crlf_row();
    test_reject_rows_with_extra_fields();
    test_parse_row_with_icon();
    test_reject_icon_that_is_a_path();
    test_reject_unsupported_flag();
    test_skip_malformed_row();
    test_find_saved_url();
    test_does_not_find_absent_url();
    puts("station_catalog tests passed");
    return 0;
}
