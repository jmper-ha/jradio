#pragma once

#include <stdbool.h>
#include <stddef.h>

/* What this device is running, and it is two answers rather than one.
 *
 * The firmware and the web interface live in different partitions and are
 * written by different commands - `idf.py flash` never touches LittleFS and
 * `idf.py littlefs-flash` touches nothing else - so a device can perfectly
 * well run an application from one commit against pages from another. That is
 * not a hypothetical: it happened on 2026-09-06, and the only way to notice
 * was to read a boot log over the serial port.
 *
 * So both are reported, side by side, and whether they agree is a question
 * this module answers rather than one the reader has to work out by comparing
 * two strings on a small screen.
 *
 * The firmware half needs nothing generated: ESP-IDF runs `git describe` and
 * puts the result in the app header, which esp_app_get_description() reads.
 * The web half is stamped into the image by tools/stamp_version.py.
 */

/* Room for `git describe` output. A tag, the commits since it, the hash and a
 * "-dirty" - `v10.20.30-100-gabcdef12-dirty` is 30 characters, and the app
 * header truncates at 32 anyway. */
#define VERSION_INFO_STRING_MAX 32U
/* "YYYY-MM-DD", or the app header's "Mmm dd yyyy". */
#define VERSION_INFO_DATE_MAX 16U

typedef struct {
    char version[VERSION_INFO_STRING_MAX + 1U];
    char built[VERSION_INFO_DATE_MAX + 1U];
    /* False when the web stamp could not be read at all - an image flashed
     * before this existed, or a partition that has never been written. The
     * strings are empty then, and the difference matters: "unknown" is a
     * different answer from "older than the firmware". */
    bool present;
} version_info_part_t;

typedef struct {
    version_info_part_t firmware;
    version_info_part_t web;
    /* The framework the firmware was built with, from the same app header. */
    char idf[VERSION_INFO_STRING_MAX + 1U];
} version_info_t;

/* Reads both. The web half touches the filesystem, so this is not for the
 * refresh loop - call it when the About screen opens or a request arrives. */
void version_info_read(version_info_t *info);

/* Whether the two halves came from the same build. False when the web stamp is
 * missing: unknown is not a match. */
bool version_info_matched(const version_info_t *info);

/* The one contact address the device shows. Here rather than in the screen
 * that draws it, because the web page shows the same string and two copies
 * would eventually differ. */
#define VERSION_INFO_AUTHOR "denis.zheleznov@gmail.com"

/* Parses the stamp tools/stamp_version.py writes: {"version":...,"built":...}.
 *
 * Split out and given no dependencies so the host tests can feed it the
 * awkward inputs - a truncated file, a value longer than the field, a stamp
 * with no version in it at all - none of which need a device to be wrong
 * about. `length` rather than a terminator: the caller has just read a file
 * and knows how much of the buffer it filled.
 *
 * Returns false and leaves `part` cleared unless a version was found. */
bool version_info_parse(const char *json, size_t length, version_info_part_t *part);
