#include <string.h>

#include "yandex_catalog.h"

/* A structure-aware JSON reader that allocates nothing and never rewinds: it
 * walks the answer once, copying the three fields each station contributes and
 * skipping everything else. See the header for why cJSON is not used here. */

typedef struct {
    const char *cursor;
} json_reader_t;

static void json_skip_whitespace(json_reader_t *reader)
{
    while (*reader->cursor == ' ' || *reader->cursor == '\t' || *reader->cursor == '\r' ||
           *reader->cursor == '\n') {
        ++reader->cursor;
    }
}

static bool json_accept(json_reader_t *reader, char expected)
{
    json_skip_whitespace(reader);
    if (*reader->cursor != expected) return false;
    ++reader->cursor;
    return true;
}

/* Appends one code point as UTF-8. The answer measured from the server carries
 * Cyrillic raw, so this only runs if Yandex ever starts escaping it. */
static void json_append_utf8(uint32_t code_point, char *output, size_t capacity,
                             size_t *length)
{
    char encoded[4];
    size_t size = 0U;
    if (code_point < 0x80U) {
        encoded[size++] = (char)code_point;
    } else if (code_point < 0x800U) {
        encoded[size++] = (char)(0xC0U | (code_point >> 6U));
        encoded[size++] = (char)(0x80U | (code_point & 0x3FU));
    } else {
        encoded[size++] = (char)(0xE0U | (code_point >> 12U));
        encoded[size++] = (char)(0x80U | ((code_point >> 6U) & 0x3FU));
        encoded[size++] = (char)(0x80U | (code_point & 0x3FU));
    }
    if (output == NULL || *length + size >= capacity) {
        /* Overflow is recorded by leaving *length past the end; the caller
         * checks it once, at the closing quote. */
        *length = capacity;
        return;
    }
    for (size_t index = 0U; index < size; ++index) {
        output[(*length)++] = encoded[index];
    }
}

static bool json_hex4(const char *text, uint32_t *value)
{
    uint32_t result = 0U;
    for (size_t index = 0U; index < 4U; ++index) {
        const char character = text[index];
        uint32_t digit;
        if (character >= '0' && character <= '9') {
            digit = (uint32_t)(character - '0');
        } else if (character >= 'a' && character <= 'f') {
            digit = (uint32_t)(character - 'a') + 10U;
        } else if (character >= 'A' && character <= 'F') {
            digit = (uint32_t)(character - 'A') + 10U;
        } else {
            return false;
        }
        result = (result << 4U) | digit;
    }
    *value = result;
    return true;
}

/* Reads a string. `output` may be NULL to skip one without copying, which is
 * what every key and every uninteresting value does. */
static bool json_read_string(json_reader_t *reader, char *output, size_t capacity)
{
    json_skip_whitespace(reader);
    if (*reader->cursor != '"') return false;
    ++reader->cursor;

    size_t length = 0U;
    while (*reader->cursor != '"') {
        if (*reader->cursor == '\0') return false;
        char value = *reader->cursor++;
        if (value == '\\') {
            const char escape = *reader->cursor;
            if (escape == '\0') return false;
            ++reader->cursor;
            switch (escape) {
            case '"':
            case '\\':
            case '/':
                value = escape;
                break;
            case 'b':
                value = '\b';
                break;
            case 'f':
                value = '\f';
                break;
            case 'n':
                value = '\n';
                break;
            case 'r':
                value = '\r';
                break;
            case 't':
                value = '\t';
                break;
            case 'u': {
                uint32_t code_point = 0U;
                if (!json_hex4(reader->cursor, &code_point)) return false;
                reader->cursor += 4;
                /* Half of a surrogate pair on its own is not a character. The
                 * replacement keeps the station listed under a slightly wrong
                 * name instead of dropping it entirely. */
                if (code_point >= 0xD800U && code_point <= 0xDFFFU) code_point = 0xFFFDU;
                json_append_utf8(code_point, output, capacity, &length);
                continue;
            }
            default:
                return false;
            }
        } else if ((unsigned char)value < 0x20U) {
            return false;
        }
        if (output == NULL) continue;
        if (length + 1U >= capacity) {
            length = capacity;
            continue;
        }
        output[length++] = value;
    }
    ++reader->cursor;
    if (output == NULL) return true;
    /* Refused rather than truncated: a clipped station id names a station the
     * rotor has never heard of, and the failure would surface much later. */
    if (length >= capacity) return false;
    output[length] = '\0';
    return true;
}

static bool json_skip_value(json_reader_t *reader);

static bool json_skip_container(json_reader_t *reader, char open, char close)
{
    if (!json_accept(reader, open)) return false;
    json_skip_whitespace(reader);
    if (json_accept(reader, close)) return true;
    while (true) {
        if (open == '{') {
            if (!json_read_string(reader, NULL, 0U) || !json_accept(reader, ':')) return false;
        }
        if (!json_skip_value(reader)) return false;
        json_skip_whitespace(reader);
        if (json_accept(reader, ',')) continue;
        return json_accept(reader, close);
    }
}

static bool json_skip_value(json_reader_t *reader)
{
    json_skip_whitespace(reader);
    switch (*reader->cursor) {
    case '{':
        return json_skip_container(reader, '{', '}');
    case '[':
        return json_skip_container(reader, '[', ']');
    case '"':
        return json_read_string(reader, NULL, 0U);
    case '\0':
        return false;
    default:
        /* A number, true, false or null: everything up to the next separator. */
        while (*reader->cursor != '\0' && *reader->cursor != ',' && *reader->cursor != '}' &&
               *reader->cursor != ']' && *reader->cursor != ' ' && *reader->cursor != '\n' &&
               *reader->cursor != '\r' && *reader->cursor != '\t') {
            ++reader->cursor;
        }
        return true;
    }
}

/* Positions the reader on the value of `key` inside the object that starts
 * here, skipping every other member. False when the object has no such key,
 * which is a malformed answer for every key this file looks for. */
static bool json_enter_object_key(json_reader_t *reader, const char *key)
{
    if (!json_accept(reader, '{')) return false;
    json_skip_whitespace(reader);
    if (json_accept(reader, '}')) return false;
    while (true) {
        char name[32];
        if (!json_read_string(reader, name, sizeof(name))) {
            /* A key too long for the buffer is simply not one we want, but the
             * reader has to get past it either way. */
            name[0] = '\0';
        }
        if (!json_accept(reader, ':')) return false;
        if (strcmp(name, key) == 0) return true;
        if (!json_skip_value(reader)) return false;
        json_skip_whitespace(reader);
        if (json_accept(reader, ',')) continue;
        return false;
    }
}

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
