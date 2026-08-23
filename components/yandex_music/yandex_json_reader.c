#include <string.h>

#include "yandex_json_reader.h"

void json_skip_whitespace(json_reader_t *reader)
{
    while (*reader->cursor == ' ' || *reader->cursor == '\t' || *reader->cursor == '\r' ||
           *reader->cursor == '\n') {
        ++reader->cursor;
    }
}

bool json_accept(json_reader_t *reader, char expected)
{
    json_skip_whitespace(reader);
    if (*reader->cursor != expected) return false;
    ++reader->cursor;
    return true;
}

/* Appends one code point as UTF-8. The answer measured from the server carries
 * Cyrillic raw, so this only runs if Yandex ever starts escaping it. */
static void json_append_utf8(uint32_t code_point, char *output, size_t capacity,
                             size_t *length, size_t *kept)
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
         * checks it once, at the closing quote. `kept` stops here, so a
         * clipping caller never splits this character in half. */
        *length = capacity;
        return;
    }
    for (size_t index = 0U; index < size; ++index) {
        output[(*length)++] = encoded[index];
    }
    *kept = *length;
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

/* The shared body of the two string readers. `kept` ends at the last complete
 * character that fitted, which is not the same as the last byte that fitted:
 * clipping a UTF-8 sequence in the middle produces bytes no renderer can draw,
 * and the display would show a replacement box for a character it did have. */
static bool json_read_string_into(json_reader_t *reader, char *output, size_t capacity,
                                  bool clip)
{
    json_skip_whitespace(reader);
    if (*reader->cursor != '"') return false;
    ++reader->cursor;

    size_t length = 0U;
    size_t kept = 0U;
    size_t pending = 0U;
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
                json_append_utf8(code_point, output, capacity, &length, &kept);
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
        /* Raw bytes arrive one at a time, so a multi-byte character only
         * becomes complete when the count its lead byte announced has all
         * been copied. Until then `kept` stays behind it. */
        const unsigned char byte = (unsigned char)value;
        if ((byte & 0x80U) == 0U) {
            pending = 0U;
            kept = length;
        } else if ((byte & 0xC0U) == 0x80U) {
            if (pending > 0U && --pending == 0U) kept = length;
        } else {
            pending = (byte >= 0xF0U) ? 3U : (byte >= 0xE0U) ? 2U : 1U;
        }
    }
    ++reader->cursor;
    if (output == NULL) return true;
    if (length >= capacity) {
        if (!clip) return false;
        output[kept] = '\0';
        return true;
    }
    output[length] = '\0';
    return true;
}

bool json_read_string(json_reader_t *reader, char *output, size_t capacity)
{
    /* Refused rather than truncated: a clipped station id names a station the
     * rotor has never heard of, and the failure would surface much later. */
    return json_read_string_into(reader, output, capacity, false);
}

bool json_read_string_clipped(json_reader_t *reader, char *output, size_t capacity)
{
    return json_read_string_into(reader, output, capacity, true);
}

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

bool json_read_uint(json_reader_t *reader, uint32_t *value)
{
    json_skip_whitespace(reader);
    if (*reader->cursor < '0' || *reader->cursor > '9') return false;
    uint32_t result = 0U;
    while (*reader->cursor >= '0' && *reader->cursor <= '9') {
        const uint32_t digit = (uint32_t)(*reader->cursor - '0');
        /* Refused rather than wrapped: a duration that silently became small
         * would look like a track that ends the moment it starts. */
        if (result > (UINT32_MAX - digit) / 10U) return false;
        result = result * 10U + digit;
        ++reader->cursor;
    }
    /* A fraction or an exponent is a different type than the caller asked for.
     * Every count this parser reads is whole, so this means a changed answer. */
    if (*reader->cursor == '.' || *reader->cursor == 'e' || *reader->cursor == 'E') return false;
    *value = result;
    return true;
}

bool json_read_bool(json_reader_t *reader, bool *value)
{
    json_skip_whitespace(reader);
    if (strncmp(reader->cursor, "true", 4) == 0) {
        reader->cursor += 4;
        *value = true;
        return true;
    }
    if (strncmp(reader->cursor, "false", 5) == 0) {
        reader->cursor += 5;
        *value = false;
        return true;
    }
    return false;
}

bool json_skip_value(json_reader_t *reader)
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

bool json_enter_object_key(json_reader_t *reader, const char *key)
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
