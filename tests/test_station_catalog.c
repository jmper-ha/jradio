#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "station_catalog.h"

static void test_parse_valid_row(void)
{
    station_catalog_entry_t entry;

    assert(station_catalog_parse_line(
        "Radio Montmartre\thttp://montmartre.ice.infomaniak.ch/montmartre.mp3\tS\n", &entry));
    assert(strcmp(entry.name, "Radio Montmartre") == 0);
    assert(strcmp(entry.url, "http://montmartre.ice.infomaniak.ch/montmartre.mp3") == 0);
    assert(entry.flag == 0);
}

static void test_parse_crlf_row(void)
{
    station_catalog_entry_t entry;

    assert(station_catalog_parse_line(
        "Technolovers - Funky House\thttp://stream1-technolovers.radiohost.de/funky-house\tL\r\n",
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
    assert(!station_catalog_parse_line("A\tB\tS\tC\tD\n", &entry));
}

/* The fourth column names the station's picture, and it is optional. */
static void test_parse_row_with_icon(void)
{
    station_catalog_entry_t entry;

    assert(station_catalog_parse_line("A\thttp://x\tL\ts7f3a91c.png\n", &entry));
    assert(strcmp(entry.icon, "s7f3a91c.png") == 0);
    assert(entry.flag == 1);

    /* JPEG is stored under its own extension, because the picture is handed
       back over HTTP and the type has to be told from the name. */
    assert(station_catalog_parse_line("A\thttp://x\tS\ts7f3a91c.jpg\n", &entry));
    assert(strcmp(entry.icon, "s7f3a91c.jpg") == 0);

    assert(station_catalog_parse_line("A\thttp://x\tL\n", &entry));
    assert(entry.icon[0] == '\0');

    /* An empty fourth column is simply no picture. */
    assert(station_catalog_parse_line("A\thttp://x\tS\t\n", &entry));
    assert(entry.icon[0] == '\0');
}

/* The name is joined to one directory, so anything that could reach out of it
   takes the whole line down rather than being quietly cleaned up. */
static void test_reject_icon_that_is_a_path(void)
{
    station_catalog_entry_t entry;

    assert(!station_catalog_parse_line("A\thttp://x\tS\t../secret.png\n", &entry));
    assert(!station_catalog_parse_line("A\thttp://x\tS\tsub/dir.png\n", &entry));
    assert(!station_catalog_parse_line("A\thttp://x\tS\t.hidden\n", &entry));
    assert(!station_catalog_icon_is_valid("a b.png"));
    assert(station_catalog_icon_is_valid(""));
    assert(station_catalog_icon_is_valid("s-1_2.png"));
}

/* A playlist written for another device carries a volume correction in the
   third column instead of the letter. This one has no per-station gain, so
   the number is read and dropped and the station keeps its name and address -
   which used to be the one thing that did not happen: every correction that
   was not 0 or 1 took the line down with it. */
static void test_parse_foreign_row_with_volume_correction(void)
{
    station_catalog_entry_t entry;

    const char *const rows[] = {
        "Foreign\thttp://example.invalid/stream\t0\n",
        "Foreign\thttp://example.invalid/stream\t-3\n",
        "Foreign\thttp://example.invalid/stream\t+2\r\n",
        // A line that simply ends in a tab says no more than one without it.
        "Foreign\thttp://example.invalid/stream\t\n",
        // And plenty of lists carry nothing but the two columns.
        "Foreign\thttp://example.invalid/stream\n",
    };
    for (size_t index = 0; index < sizeof(rows) / sizeof(rows[0]); ++index) {
        assert(station_catalog_parse_line(rows[index], &entry));
        assert(strcmp(entry.name, "Foreign") == 0);
        assert(strcmp(entry.url, "http://example.invalid/stream") == 0);
        /* Nothing was said about the name, so the stream gets to announce it,
           and there is no column a picture could have come from. */
        assert(entry.flag == 0);
        assert(entry.icon[0] == '\0');
    }
}

/* The letter is a letter: neither the numbers the column used to hold nor
   anything else stands in for it. */
static void test_reject_unsupported_third_column(void)
{
    station_catalog_entry_t entry;

    assert(!station_catalog_parse_line("A\tB\tSL\n", &entry));
    assert(!station_catalog_parse_line("A\tB\ts\n", &entry));
    assert(!station_catalog_parse_line("A\tB\tjazz\n", &entry));
    assert(!station_catalog_parse_line("A\tB\t1.5\n", &entry));
    // A picture needs the letter: no other dialect has a fourth column.
    assert(!station_catalog_parse_line("A\tB\t0\ts7f3a91c.png\n", &entry));
}

static void test_skip_malformed_row(void)
{
    station_catalog_t catalog = {0};
    const char *text =
        "bad row without tabs\n"
        "Jazz Radio Lounge\thttps://jazzlounge.ice.infomaniak.ch/jazzlounge-high.mp3\tS\n"
        "too\tmany\tfields\tS\tover\n";

    assert(station_catalog_load_text(text, &catalog));
    assert(catalog.count == 1);
    assert(strcmp(catalog.entries[0].name, "Jazz Radio Lounge") == 0);
}

/* The longest line the fields allow has to survive the copy the reader makes
   of it. The buffer was the sum of two of the three lengths and skipped it. */
static void test_longest_line_is_read(void)
{
    station_catalog_t catalog = {0};
    char text[STATION_CATALOG_LINE_MAX_LEN + 2];
    char name[STATION_CATALOG_NAME_MAX_LEN];
    char url[STATION_CATALOG_URL_MAX_LEN];
    char icon[STATION_CATALOG_ICON_MAX_LEN];

    memset(name, 'n', sizeof(name) - 1U);
    name[sizeof(name) - 1U] = '\0';
    memset(url, 'u', sizeof(url) - 1U);
    url[sizeof(url) - 1U] = '\0';
    memset(icon, 'i', sizeof(icon) - 1U);
    icon[sizeof(icon) - 1U] = '\0';
    const int written = snprintf(text, sizeof(text), "%s\t%s\tL\t%s\n", name, url, icon);
    assert(written > 0 && (size_t)written < sizeof(text));

    assert(station_catalog_load_text(text, &catalog));
    assert(catalog.count == 1);
    assert(strcmp(catalog.entries[0].icon, icon) == 0);
}

static void test_find_saved_url(void)
{
    station_catalog_t catalog = {0};
    size_t index = 0;

    assert(station_catalog_load_text(
        "Radio Montmartre\thttp://montmartre.ice.infomaniak.ch/montmartre.mp3\tS\n"
        "Jazz Radio Lounge\thttps://jazzlounge.ice.infomaniak.ch/jazzlounge-high.mp3\tS\n",
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
        "Radio Montmartre\thttp://montmartre.ice.infomaniak.ch/montmartre.mp3\tS\n", &catalog));
    assert(!station_catalog_find_by_url(&catalog, "http://missing.example/stream", &index));
}

int main(void)
{
    test_parse_valid_row();
    test_parse_crlf_row();
    test_reject_rows_with_extra_fields();
    test_parse_row_with_icon();
    test_reject_icon_that_is_a_path();
    test_parse_foreign_row_with_volume_correction();
    test_reject_unsupported_third_column();
    test_skip_malformed_row();
    test_longest_line_is_read();
    test_find_saved_url();
    test_does_not_find_absent_url();
    puts("station_catalog tests passed");
    return 0;
}
