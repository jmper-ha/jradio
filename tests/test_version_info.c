#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "version_info.h"

/* The stamp comes off a filesystem, so what matters is not that it parses the
   file tools/stamp_version.py writes - it will - but that it refuses
   everything else without reading past the buffer it was handed. A truncated
   image, a partition written by an older firmware, a file somebody edited: all
   of them have to come back as "no version", which the caller already handles
   because an image flashed before the stamp existed has no file at all. */

static void test_it_reads_what_the_generator_writes(void)
{
    /* Byte for byte what tools/stamp_version.py emits: compact separators, no
       spaces, a trailing newline. */
    const char json[] = "{\"version\":\"v1.2.0-3-gabc1234\",\"built\":\"2026-09-06\"}\n";
    version_info_part_t part;
    assert(version_info_parse(json, strlen(json), &part));
    assert(part.present);
    assert(strcmp(part.version, "v1.2.0-3-gabc1234") == 0);
    assert(strcmp(part.built, "2026-09-06") == 0);
}

static void test_it_survives_the_shapes_a_hand_written_file_takes(void)
{
    version_info_part_t part;
    /* Whitespace around the colon, and the fields the other way round. */
    const char spaced[] = "{ \"built\" : \"2026-01-01\" ,\n  \"version\" : \"abc1234\" }";
    assert(version_info_parse(spaced, strlen(spaced), &part));
    assert(strcmp(part.version, "abc1234") == 0);
    assert(strcmp(part.built, "2026-01-01") == 0);

    /* A date is optional; a version is the whole point. */
    const char no_date[] = "{\"version\":\"abc1234\"}";
    assert(version_info_parse(no_date, strlen(no_date), &part));
    assert(part.present);
    assert(strcmp(part.version, "abc1234") == 0);
    assert(part.built[0] == '\0');
}

static void test_it_refuses_what_it_cannot_read(void)
{
    version_info_part_t part;

    /* Nothing at all, and nothing useful. */
    assert(!version_info_parse(NULL, 10U, &part));
    assert(!version_info_parse("", 0U, &part));
    assert(!version_info_parse("{}", 2U, &part));
    assert(!part.present);
    assert(part.version[0] == '\0');

    /* Cut off mid-value: no closing quote before the end of the buffer. */
    const char truncated[] = "{\"version\":\"v1.2.0-3-gab";
    assert(!version_info_parse(truncated, strlen(truncated), &part));
    assert(part.version[0] == '\0');

    /* The key is there and holds something that is not a string. Stops rather
       than hunting for another key of the same name. */
    const char number[] = "{\"version\":17}";
    assert(!version_info_parse(number, strlen(number), &part));
    const char null_value[] = "{\"version\":null,\"built\":\"2026-09-06\"}";
    assert(!version_info_parse(null_value, strlen(null_value), &part));

    /* Empty is not a version. */
    const char empty[] = "{\"version\":\"\"}";
    assert(!version_info_parse(empty, strlen(empty), &part));

    /* Longer than the field. Refused outright rather than clipped: half a
       version string read as a whole one is worse than admitting ignorance. */
    char oversized[VERSION_INFO_STRING_MAX + 64U];
    int written = snprintf(oversized, sizeof(oversized), "{\"version\":\"");
    for (size_t index = 0U; index < VERSION_INFO_STRING_MAX + 8U; ++index) {
        oversized[(size_t)written + index] = 'v';
    }
    written += (int)(VERSION_INFO_STRING_MAX + 8U);
    written += snprintf(oversized + written, sizeof(oversized) - (size_t)written, "\"}");
    assert(!version_info_parse(oversized, (size_t)written, &part));
    assert(part.version[0] == '\0');

    /* A name that merely starts the same is a different name. */
    const char lookalike[] = "{\"version_of_something\":\"abc1234\"}";
    assert(!version_info_parse(lookalike, strlen(lookalike), &part));
}

static void test_it_never_reads_past_the_length_it_is_given(void)
{
    /* The buffer holds a whole valid stamp, but the caller says the file was
       shorter than that - which is what a short read looks like. Every field
       must be found inside the length, not inside the buffer. */
    const char json[] = "{\"version\":\"abc1234\",\"built\":\"2026-09-06\"}";
    version_info_part_t part;
    for (size_t length = 0U; length < strlen(json); ++length) {
        /* No assertion on the answer: some prefixes hold a complete version
           field and legitimately parse. The point is that none of them run
           off the end - the sanitizer decides whether this passed. */
        (void)version_info_parse(json, length, &part);
    }
}

static void test_a_missing_web_stamp_is_not_a_match(void)
{
    version_info_t info;
    memset(&info, 0, sizeof(info));
    snprintf(info.firmware.version, sizeof(info.firmware.version), "abc1234");
    info.firmware.present = true;

    /* Unknown is not agreement: reporting a match here would hide precisely
       the case this exists to show. */
    assert(!version_info_matched(&info));

    snprintf(info.web.version, sizeof(info.web.version), "abc1234");
    info.web.present = true;
    assert(version_info_matched(&info));

    snprintf(info.web.version, sizeof(info.web.version), "def5678");
    assert(!version_info_matched(&info));

    /* And a firmware that could not name itself is no better than a web half
       that could not. */
    info.firmware.present = false;
    snprintf(info.web.version, sizeof(info.web.version), "abc1234");
    assert(!version_info_matched(&info));

    assert(!version_info_matched(NULL));
}

int main(void)
{
    test_it_reads_what_the_generator_writes();
    test_it_survives_the_shapes_a_hand_written_file_takes();
    test_it_refuses_what_it_cannot_read();
    test_it_never_reads_past_the_length_it_is_given();
    test_a_missing_web_stamp_is_not_a_match();
    puts("version_info tests passed");
    return 0;
}
