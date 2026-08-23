#include <string.h>

#include "yandex_catalog.h"
#include "yandex_json_reader.h"

static bool yandex_parse_station(json_reader_t *reader, yandex_station_t *station)
{
    *station = (yandex_station_t){0};
    char type[YANDEX_STATION_ID_MAX + 1] = {0};
    char tag[YANDEX_STATION_ID_MAX + 1] = {0};
    bool have_id = false;
    bool have_name = false;

    if (!json_accept(reader, '{')) return false;
    json_skip_whitespace(reader);
    if (json_accept(reader, '}')) return false;
    while (true) {
        char key[32];
        if (!json_read_string(reader, key, sizeof(key))) key[0] = '\0';
        if (!json_accept(reader, ':')) return false;

        if (strcmp(key, "id") == 0) {
            json_reader_t identity = *reader;
            if (!json_enter_object_key(&identity, "type") ||
                !json_read_string(&identity, type, sizeof(type))) {
                return false;
            }
            identity = *reader;
            if (!json_enter_object_key(&identity, "tag") ||
                !json_read_string(&identity, tag, sizeof(tag))) {
                return false;
            }
            have_id = true;
            if (!json_skip_value(reader)) return false;
        } else if (strcmp(key, "name") == 0) {
            if (!json_read_string(reader, station->name, sizeof(station->name))) return false;
            have_name = true;
        } else if (strcmp(key, "idForFrom") == 0) {
            if (!json_read_string(reader, station->from, sizeof(station->from))) return false;
        } else if (!json_skip_value(reader)) {
            return false;
        }

        json_skip_whitespace(reader);
        if (json_accept(reader, ',')) continue;
        if (!json_accept(reader, '}')) return false;
        break;
    }

    if (!have_id || !have_name || type[0] == '\0' || tag[0] == '\0') return false;
    /* "user" + ":" + "onyourwave", the form every rotor endpoint takes. */
    if (strlen(type) + 1U + strlen(tag) > YANDEX_STATION_ID_MAX) return false;
    strcpy(station->id, type);
    strcat(station->id, ":");
    strcat(station->id, tag);
    return true;
}

bool yandex_catalog_parse_dashboard(const char *json, yandex_catalog_t *catalog)
{
    if (json == NULL || catalog == NULL) return false;
    *catalog = (yandex_catalog_t){0};

    json_reader_t reader = {.cursor = json};
    /* The device sees the whole answer, the tests sometimes only its payload;
     * accepting both keeps a fixture from having to carry the envelope. */
    json_reader_t probe = reader;
    if (json_enter_object_key(&probe, "result")) {
        reader = probe;
    }
    if (!json_enter_object_key(&reader, "stations")) return false;
    if (!json_accept(&reader, '[')) return false;
    json_skip_whitespace(&reader);
    if (json_accept(&reader, ']')) return true;

    while (true) {
        json_reader_t entry = reader;
        yandex_station_t station;
        if (json_enter_object_key(&entry, "station") &&
            yandex_parse_station(&entry, &station) &&
            catalog->count < YANDEX_CATALOG_MAX_STATIONS) {
            catalog->stations[catalog->count++] = station;
        }
        /* Whatever the element turned out to be, the walk continues from its
         * end: one unreadable station must not cost the rest of the list. */
        if (!json_skip_value(&reader)) return catalog->count > 0U;
        json_skip_whitespace(&reader);
        if (json_accept(&reader, ',')) continue;
        break;
    }
    return catalog->count > 0U;
}

bool yandex_catalog_equal(const yandex_catalog_t *left, const yandex_catalog_t *right)
{
    if (left == NULL || right == NULL) return left == right;
    if (left->count != right->count) return false;
    for (uint8_t index = 0U; index < left->count; ++index) {
        if (strcmp(left->stations[index].id, right->stations[index].id) != 0 ||
            strcmp(left->stations[index].name, right->stations[index].name) != 0 ||
            strcmp(left->stations[index].from, right->stations[index].from) != 0) {
            return false;
        }
    }
    return true;
}
