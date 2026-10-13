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

/* The seeds of a wave, joined into a station id. False when one is not a
 * string or the whole will not fit - a station id cut short is a different
 * station. */
static bool yandex_parse_seeds(json_reader_t *reader, char *id, size_t capacity)
{
    if (!json_accept(reader, '[')) return false;
    id[0] = '\0';
    size_t length = 0U;
    json_skip_whitespace(reader);
    if (json_accept(reader, ']')) return false;
    bool fits = true;
    while (true) {
        char seed[YANDEX_STATION_ID_MAX + 1];
        if (!json_read_string(reader, seed, sizeof(seed))) {
            fits = false;
            if (!json_skip_value(reader)) return false;
        } else {
            const size_t separator = length > 0U ? 1U : 0U;
            const size_t seed_length = strlen(seed);
            if (seed_length == 0U || length + separator + seed_length >= capacity) {
                fits = false;
            } else {
                if (separator > 0U) id[length++] = YANDEX_STATION_SEED_SEPARATOR;
                memcpy(id + length, seed, seed_length + 1U);
                length += seed_length;
            }
        }
        json_skip_whitespace(reader);
        if (json_accept(reader, ',')) continue;
        if (!json_accept(reader, ']')) return false;
        return fits && length > 0U;
    }
}

/* One item of the wheel: {"type":"WAVE","style":...,"data":{"wave":{"name",
 * "seeds"}}}. The wave's own "description" and the agent's cover sit beside
 * them, so the walk goes by structure here too. */
static bool yandex_parse_wave(json_reader_t *reader, yandex_station_t *station)
{
    *station = (yandex_station_t){0};
    char type[16] = {0};
    char style[32] = {0};
    bool have_wave = false;

    if (!json_accept(reader, '{')) return false;
    json_skip_whitespace(reader);
    if (json_accept(reader, '}')) return false;
    while (true) {
        char key[32];
        if (!json_read_string(reader, key, sizeof(key))) key[0] = '\0';
        if (!json_accept(reader, ':')) return false;

        if (strcmp(key, "type") == 0) {
            if (!json_read_string(reader, type, sizeof(type))) type[0] = '\0';
        } else if (strcmp(key, "style") == 0) {
            if (!json_read_string(reader, style, sizeof(style))) style[0] = '\0';
        } else if (strcmp(key, "data") == 0) {
            json_reader_t wave = *reader;
            if (json_enter_object_key(&wave, "wave")) {
                json_reader_t name = wave;
                json_reader_t seeds = wave;
                have_wave = json_enter_object_key(&name, "name") &&
                            json_read_string_clipped(&name, station->name,
                                                     sizeof(station->name)) &&
                            station->name[0] != '\0' &&
                            json_enter_object_key(&seeds, "seeds") &&
                            yandex_parse_seeds(&seeds, station->id, sizeof(station->id));
            }
            if (!json_skip_value(reader)) return false;
        } else if (!json_skip_value(reader)) {
            return false;
        }

        json_skip_whitespace(reader);
        if (json_accept(reader, ',')) continue;
        if (!json_accept(reader, '}')) return false;
        break;
    }
    /* "CONTROL_ACCENT" is how the wheel marks its two controls; a style the
     * wheel has not used yet is taken as a station. */
    return have_wave && strcmp(type, "WAVE") == 0 && strncmp(style, "CONTROL", 7) != 0;
}

size_t yandex_catalog_parse_wheel(const char *json, yandex_catalog_t *catalog)
{
    if (json == NULL || catalog == NULL) return 0U;
    json_reader_t reader = {.cursor = json};
    json_reader_t probe = reader;
    if (json_enter_object_key(&probe, "result")) reader = probe;
    if (!json_enter_object_key(&reader, "items")) return 0U;
    if (!json_accept(&reader, '[')) return 0U;
    json_skip_whitespace(&reader);
    if (json_accept(&reader, ']')) return 0U;

    size_t added = 0U;
    while (true) {
        json_reader_t entry = reader;
        yandex_station_t station;
        if (catalog->count < YANDEX_CATALOG_MAX_STATIONS &&
            yandex_parse_wave(&entry, &station)) {
            bool listed = false;
            for (uint8_t index = 0U; index < catalog->count && !listed; ++index) {
                listed = strcmp(catalog->stations[index].id, station.id) == 0;
            }
            if (!listed) {
                catalog->stations[catalog->count++] = station;
                ++added;
            }
        }
        if (!json_skip_value(&reader)) return added;
        json_skip_whitespace(&reader);
        if (json_accept(&reader, ',')) continue;
        break;
    }
    return added;
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
