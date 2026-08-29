#include "station_catalog.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static bool station_catalog_copy_field(char *destination, size_t destination_size,
                                       const char *source, size_t source_length)
{
    if (source_length == 0 || source_length >= destination_size) return false;

    memcpy(destination, source, source_length);
    destination[source_length] = '\0';
    return true;
}

bool station_catalog_icon_is_valid(const char *icon)
{
    if (icon == NULL) return false;
    if (icon[0] == '\0') return true;
    if (icon[0] == '.') return false;
    for (const char *cursor = icon; *cursor != '\0'; ++cursor) {
        const char value = *cursor;
        const bool allowed = (value >= 'a' && value <= 'z') ||
                             (value >= 'A' && value <= 'Z') ||
                             (value >= '0' && value <= '9') ||
                             value == '.' || value == '-' || value == '_';
        if (!allowed) return false;
    }
    return true;
}

bool station_catalog_parse_line(const char *line, station_catalog_entry_t *entry)
{
    const char *first_tab;
    const char *second_tab;
    const char *third_tab;
    const char *end;
    char flag_text[16];
    char *flag_end;
    long flag;

    if (line == NULL || entry == NULL) return false;

    end = line + strlen(line);
    while (end > line && (end[-1] == '\n' || end[-1] == '\r')) --end;

    first_tab = memchr(line, '\t', (size_t)(end - line));
    if (first_tab == NULL) return false;
    second_tab = memchr(first_tab + 1, '\t', (size_t)(end - (first_tab + 1)));
    if (second_tab == NULL) return false;
    /* The picture is optional and last: a line written before the column
     * existed has three fields and still parses, which is what keeps an old
     * playlist working. A fourth tab is still a malformed line. */
    third_tab = memchr(second_tab + 1, '\t', (size_t)(end - (second_tab + 1)));
    if (third_tab != NULL &&
        memchr(third_tab + 1, '\t', (size_t)(end - (third_tab + 1))) != NULL) {
        return false;
    }
    const char *flag_end_of_field = third_tab == NULL ? end : third_tab;

    entry->icon[0] = '\0';
    if (!station_catalog_copy_field(entry->name, sizeof(entry->name), line,
                                    (size_t)(first_tab - line)) ||
        !station_catalog_copy_field(entry->url, sizeof(entry->url), first_tab + 1,
                                    (size_t)(second_tab - (first_tab + 1))) ||
        !station_catalog_copy_field(flag_text, sizeof(flag_text), second_tab + 1,
                                    (size_t)(flag_end_of_field - (second_tab + 1)))) {
        return false;
    }
    /* An empty fourth column is no picture, not a malformed field: it is what
     * the editor writes for a station that has none. */
    if (third_tab != NULL && end > third_tab + 1) {
        if (!station_catalog_copy_field(entry->icon, sizeof(entry->icon), third_tab + 1,
                                        (size_t)(end - (third_tab + 1)))) {
            return false;
        }
        /* A name, never a path: it is joined to /littlefs/radio_img, and a
         * separator or a leading dot in it would reach outside that
         * directory. An empty fourth column is simply no picture. */
        if (!station_catalog_icon_is_valid(entry->icon)) return false;
    }

    errno = 0;
    flag = strtol(flag_text, &flag_end, 10);
    if (errno != 0 || *flag_end != '\0' || flag < INT_MIN || flag > INT_MAX ||
        (flag != 0 && flag != 1)) return false;

    entry->flag = (int)flag;
    return true;
}

bool station_catalog_load_text(const char *text, station_catalog_t *catalog)
{
    const char *line;

    if (text == NULL || catalog == NULL) return false;

    catalog->count = 0;
    line = text;
    while (*line != '\0') {
        const char *line_end = strchr(line, '\n');
        size_t line_length = line_end == NULL ? strlen(line) : (size_t)(line_end - line + 1);
        char row[STATION_CATALOG_NAME_MAX_LEN + STATION_CATALOG_URL_MAX_LEN + 32];

        if (line_length < sizeof(row)) {
            memcpy(row, line, line_length);
            row[line_length] = '\0';
            if (catalog->count < STATION_CATALOG_MAX_ENTRIES &&
                station_catalog_parse_line(row, &catalog->entries[catalog->count])) {
                ++catalog->count;
            }
        }

        if (line_end == NULL) break;
        line = line_end + 1;
    }

    return true;
}

bool station_catalog_find_by_url(const station_catalog_t *catalog, const char *url, size_t *index)
{
    if (catalog == NULL || url == NULL || index == NULL) return false;

    for (size_t entry_index = 0; entry_index < catalog->count; ++entry_index) {
        if (strcmp(catalog->entries[entry_index].url, url) == 0) {
            *index = entry_index;
            return true;
        }
    }
    return false;
}

bool station_catalog_append_if_missing(station_catalog_t *catalog,
                                       const station_catalog_entry_t *entry)
{
    size_t existing_index;

    if (catalog == NULL || entry == NULL || entry->url[0] == '\0' ||
        station_catalog_find_by_url(catalog, entry->url, &existing_index)) {
        return false;
    }
    if (catalog->count >= STATION_CATALOG_MAX_ENTRIES) return false;

    catalog->entries[catalog->count++] = *entry;
    return true;
}
