#pragma once

#include <stdbool.h>
#include <stddef.h>

#define STATION_CATALOG_PATH "/littlefs/config/stations.csv"
#define STATION_CATALOG_TEMP_PATH "/littlefs/config/stations.tmp"
/* What the catalogue holds. The count is the number the on-device list can be
 * scrolled through and the number the two-digit index in it can name; the two
 * lengths are what a station line may carry.
 *
 * The whole catalogue is one array of these - 35 KB at these numbers - so it
 * lives in PSRAM rather than in .bss. Both buffers on the playlist path (the
 * file read in internet_radio.c and the upload limit in web_server.c) are
 * derived from these three numbers rather than typed, so raising the count
 * again cannot leave a buffer behind that silently truncates the file. */
#define STATION_CATALOG_MAX_ENTRIES 99
#define STATION_CATALOG_NAME_MAX_LEN 96
#define STATION_CATALOG_URL_MAX_LEN 256
/* One line at its longest: name, separator, url, flag, newline, and room to
 * spare. */
#define STATION_CATALOG_LINE_MAX_LEN \
    (STATION_CATALOG_NAME_MAX_LEN + STATION_CATALOG_URL_MAX_LEN + 8U)
/* The whole file at its longest, with the terminator. */
#define STATION_CATALOG_TEXT_MAX_LEN \
    (STATION_CATALOG_MAX_ENTRIES * STATION_CATALOG_LINE_MAX_LEN + 1U)

typedef struct {
    char name[STATION_CATALOG_NAME_MAX_LEN];
    char url[STATION_CATALOG_URL_MAX_LEN];
    int flag;
} station_catalog_entry_t;

typedef struct {
    station_catalog_entry_t entries[STATION_CATALOG_MAX_ENTRIES];
    size_t count;
} station_catalog_t;

bool station_catalog_parse_line(const char *line, station_catalog_entry_t *entry);
bool station_catalog_load_text(const char *text, station_catalog_t *catalog);
bool station_catalog_find_by_url(const station_catalog_t *catalog, const char *url,
                                 size_t *index);
bool station_catalog_append_if_missing(station_catalog_t *catalog,
                                       const station_catalog_entry_t *entry);
