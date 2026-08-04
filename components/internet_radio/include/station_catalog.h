#pragma once

#include <stdbool.h>
#include <stddef.h>

#define STATION_CATALOG_PATH "/littlefs/config/stations.csv"
#define STATION_CATALOG_MAX_ENTRIES 32
#define STATION_CATALOG_NAME_MAX_LEN 96
#define STATION_CATALOG_URL_MAX_LEN 256

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
