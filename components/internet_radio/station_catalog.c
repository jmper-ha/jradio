#include "station_catalog.h"

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

/* Which dialect a line is written in is decided by its third column.
 *
 * Ours puts a letter there - `S` for the name the stream announces, `L` for
 * the one kept in the list - and may add the picture as a fourth column.
 * Playlists written for other devices put a volume correction there instead:
 * a signed integer, almost always 0, which this device has nowhere to put -
 * its volume is one setting and not a per-station gain. Those lines are read
 * for their name and address alone, and so are lists carrying nothing but
 * those two, which is the other shape found in the wild.
 *
 * The letter is what makes the two tellable apart. The column used to hold 0
 * or 1, which is what a correction of 0 or +1 dB is written with as well, so
 * a foreign `1` was read as "name from the list" and every other correction
 * took the whole line down as malformed. */
#define STATION_CATALOG_FIELDS_MAX 4U

/* Optionally signed decimal, which is all a volume correction ever is. An
 * empty column counts as one: a line that ends in a tab has one, and it says
 * no more than a missing column does. */
static bool station_catalog_is_volume_correction(const char *field, size_t length)
{
    if (length == 0U) return true;
    if (length > 12U) return false;
    size_t index = (field[0] == '-' || field[0] == '+') ? 1U : 0U;
    if (index >= length) return false;
    for (; index < length; ++index) {
        if (field[index] < '0' || field[index] > '9') return false;
    }
    return true;
}

bool station_catalog_parse_line(const char *line, station_catalog_entry_t *entry)
{
    const char *start[STATION_CATALOG_FIELDS_MAX];
    size_t length[STATION_CATALOG_FIELDS_MAX];
    size_t count = 0U;

    if (line == NULL || entry == NULL) return false;

    const char *end = line + strlen(line);
    while (end > line && (end[-1] == '\n' || end[-1] == '\r')) --end;

    for (const char *cursor = line;;) {
        const char *tab = memchr(cursor, '\t', (size_t)(end - cursor));
        const char *field_end = tab == NULL ? end : tab;
        // A fifth column is a malformed line in either dialect.
        if (count == STATION_CATALOG_FIELDS_MAX) return false;
        start[count] = cursor;
        length[count] = (size_t)(field_end - cursor);
        ++count;
        if (tab == NULL) break;
        cursor = tab + 1;
    }
    if (count < 2U) return false;

    entry->icon[0] = '\0';
    if (!station_catalog_copy_field(entry->name, sizeof(entry->name), start[0], length[0]) ||
        !station_catalog_copy_field(entry->url, sizeof(entry->url), start[1], length[1])) {
        return false;
    }

    const char kind = (count > 2U && length[2] == 1U) ? start[2][0] : '\0';
    if (kind == 'S' || kind == 'L') {
        entry->flag = kind == 'L' ? 1 : 0;
        /* An empty fourth column is no picture rather than a malformed field:
         * it is what the editor writes for a station that has none. */
        if (count == STATION_CATALOG_FIELDS_MAX && length[3] > 0U) {
            if (!station_catalog_copy_field(entry->icon, sizeof(entry->icon), start[3],
                                            length[3])) {
                return false;
            }
            /* A name, never a path: it is joined to /littlefs/radio_img, and a
             * separator or a leading dot in it would reach outside that
             * directory. */
            if (!station_catalog_icon_is_valid(entry->icon)) return false;
        }
        return true;
    }

    /* Not ours, so at most the two columns every list has plus a correction.
     * A picture cannot ride along - nothing writes one in that dialect - and
     * the name to show is the one the stream announces, which is what a list
     * that says nothing about it leaves the device free to use. */
    if (count > 3U) return false;
    if (count == 3U && !station_catalog_is_volume_correction(start[2], length[2])) return false;
    entry->flag = 0;
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
        /* The constant exists for exactly this, and the sum written out here
           was 8 bytes short of it: a line with a long name, a long address
           and a picture did not fit and was skipped without a word. */
        char row[STATION_CATALOG_LINE_MAX_LEN];

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
