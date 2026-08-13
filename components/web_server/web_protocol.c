#include "web_protocol.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "cJSON.h"

enum {
    FIELD_TYPE = 1U << 0,
    FIELD_ID = 1U << 1,
    FIELD_ACTION = 1U << 2,
    FIELD_SOURCE = 1U << 3,
    FIELD_INDEX = 1U << 4,
    FIELD_SSID = 1U << 5,
    FIELD_PASSWORD = 1U << 6,
};

#define COMMON_FIELDS (FIELD_TYPE | FIELD_ID | FIELD_ACTION)
#define WEB_PROTOCOL_INDEX_MAX UINT32_MAX
#define WEB_PROTOCOL_JSON_MAX_DEPTH 12U

typedef enum {
    RAW_INDEX_NOT_FOUND = 0,
    RAW_INDEX_VALID,
    RAW_INDEX_INVALID,
} raw_index_result_t;

static void secure_zero(void *memory, size_t size)
{
    volatile unsigned char *bytes = (volatile unsigned char *)memory;
    while (size-- > 0U) {
        *bytes++ = 0U;
    }
}

void web_protocol_clear_command(web_command_t *command)
{
    if (command != NULL) {
        secure_zero(command, sizeof(*command));
    }
}

static bool utf8_decode(const unsigned char *text, size_t length, size_t *offset,
                        uint32_t *codepoint)
{
    const unsigned char first = text[*offset];
    if (first < 0x80U) {
        *codepoint = first;
        ++*offset;
        return true;
    }

    size_t sequence_length;
    uint32_t value;
    uint32_t minimum;
    if (first >= 0xC2U && first <= 0xDFU) {
        sequence_length = 2U;
        value = first & 0x1FU;
        minimum = 0x80U;
    } else if (first >= 0xE0U && first <= 0xEFU) {
        sequence_length = 3U;
        value = first & 0x0FU;
        minimum = 0x800U;
    } else if (first >= 0xF0U && first <= 0xF4U) {
        sequence_length = 4U;
        value = first & 0x07U;
        minimum = 0x10000U;
    } else {
        return false;
    }
    if (sequence_length > length - *offset) {
        return false;
    }
    for (size_t index = 1U; index < sequence_length; ++index) {
        const unsigned char continuation = text[*offset + index];
        if ((continuation & 0xC0U) != 0x80U) {
            return false;
        }
        value = (value << 6) | (uint32_t)(continuation & 0x3FU);
    }
    if (value < minimum || value > 0x10FFFFU ||
        (value >= 0xD800U && value <= 0xDFFFU)) {
        return false;
    }
    *offset += sequence_length;
    *codepoint = value;
    return true;
}

static bool utf8_valid(const char *text, size_t length, bool reject_controls)
{
    if (text == NULL) {
        return false;
    }
    const unsigned char *bytes = (const unsigned char *)text;
    size_t offset = 0U;
    while (offset < length) {
        uint32_t codepoint;
        if (!utf8_decode(bytes, length, &offset, &codepoint)) {
            return false;
        }
        if (reject_controls &&
            (codepoint < 0x20U || (codepoint >= 0x7FU && codepoint <= 0x9FU))) {
            return false;
        }
    }
    return true;
}

static int hex_value(unsigned char value)
{
    if (value >= '0' && value <= '9') return (int)(value - '0');
    if (value >= 'a' && value <= 'f') return (int)(value - 'a') + 10;
    if (value >= 'A' && value <= 'F') return (int)(value - 'A') + 10;
    return -1;
}

static bool json_has_no_escaped_nul(const char *frame, size_t length)
{
    bool in_string = false;
    bool escaped = false;
    for (size_t index = 0U; index < length; ++index) {
        const unsigned char value = (unsigned char)frame[index];
        if (!in_string) {
            if (value == '"') in_string = true;
            continue;
        }
        if (!escaped) {
            if (value == '\\') {
                escaped = true;
            } else if (value == '"') {
                in_string = false;
            }
            continue;
        }

        if (value == 'u' && index + 4U < length) {
            uint32_t codepoint = 0U;
            bool valid_hex = true;
            for (size_t digit = 1U; digit <= 4U; ++digit) {
                const int hex = hex_value((unsigned char)frame[index + digit]);
                if (hex < 0) {
                    valid_hex = false;
                    break;
                }
                codepoint = (codepoint << 4) | (uint32_t)hex;
            }
            if (valid_hex && codepoint == 0U) {
                return false;
            }
        }
        escaped = false;
    }
    return true;
}

static bool json_depth_safe(const char *frame, size_t length)
{
    size_t depth = 0U;
    bool in_string = false;
    bool escaped = false;
    for (size_t index = 0U; index < length; ++index) {
        const unsigned char value = (unsigned char)frame[index];
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (value == '\\') {
                escaped = true;
            } else if (value == '"') {
                in_string = false;
            }
            continue;
        }

        if (value == '"') {
            in_string = true;
        } else if (value == '{' || value == '[') {
            if (++depth > WEB_PROTOCOL_JSON_MAX_DEPTH) return false;
        } else if (value == '}' || value == ']') {
            if (depth == 0U) return false;
            --depth;
        }
    }
    return !in_string && !escaped && depth == 0U;
}

static bool json_whitespace(unsigned char value);

static cJSON *parse_document(char *buffer, size_t length,
                             const char **parse_end, bool *exact)
{
    cJSON *root = cJSON_ParseWithLengthOpts(buffer, length + 1U, parse_end,
                                            false);
    const char *document_end = *parse_end;
    while (document_end != NULL && document_end < buffer + length &&
           json_whitespace((unsigned char)*document_end)) {
        ++document_end;
    }
    *exact = root != NULL && document_end == buffer + length;
    return root;
}

static bool json_whitespace(unsigned char value)
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

static bool json_string_end(const char *frame, size_t length, size_t start,
                            size_t *end)
{
    bool escaped = false;
    for (size_t index = start + 1U; index < length; ++index) {
        const unsigned char value = (unsigned char)frame[index];
        if (escaped) {
            escaped = false;
        } else if (value == '\\') {
            escaped = true;
        } else if (value == '"') {
            *end = index;
            return true;
        }
    }
    return false;
}

static bool json_hex4(const char *frame, size_t end, size_t start,
                      uint32_t *value)
{
    if (end - start < 4U) return false;
    uint32_t decoded = 0U;
    for (size_t index = 0U; index < 4U; ++index) {
        const int hex = hex_value((unsigned char)frame[start + index]);
        if (hex < 0) return false;
        decoded = (decoded << 4) | (uint32_t)hex;
    }
    *value = decoded;
    return true;
}

static bool json_string_equals_ascii(const char *frame, size_t start,
                                     size_t end, const char *target)
{
    const size_t target_length = strlen(target);
    size_t cursor = start;
    size_t target_offset = 0U;
    while (cursor < end) {
        if (target_offset >= target_length) return false;
        uint32_t codepoint;
        const unsigned char value = (unsigned char)frame[cursor++];
        if (value != '\\') {
            if (value < 0x20U || value >= 0x80U) return false;
            codepoint = value;
        } else {
            if (cursor >= end) return false;
            const unsigned char escape = (unsigned char)frame[cursor++];
            switch (escape) {
            case '"': codepoint = '"'; break;
            case '\\': codepoint = '\\'; break;
            case '/': codepoint = '/'; break;
            case 'b': codepoint = '\b'; break;
            case 'f': codepoint = '\f'; break;
            case 'n': codepoint = '\n'; break;
            case 'r': codepoint = '\r'; break;
            case 't': codepoint = '\t'; break;
            case 'u': {
                if (!json_hex4(frame, end, cursor, &codepoint)) return false;
                cursor += 4U;
                if (codepoint >= 0xD800U && codepoint <= 0xDBFFU) {
                    uint32_t low_surrogate;
                    if (end - cursor < 6U || frame[cursor] != '\\' ||
                        frame[cursor + 1U] != 'u' ||
                        !json_hex4(frame, end, cursor + 2U, &low_surrogate) ||
                        low_surrogate < 0xDC00U || low_surrogate > 0xDFFFU) {
                        return false;
                    }
                    codepoint = 0x10000U +
                                ((codepoint - 0xD800U) << 10) +
                                (low_surrogate - 0xDC00U);
                    cursor += 6U;
                } else if (codepoint >= 0xDC00U && codepoint <= 0xDFFFU) {
                    return false;
                }
                break;
            }
            default:
                return false;
            }
        }
        if (codepoint != (unsigned char)target[target_offset++]) return false;
    }
    return target_offset == target_length;
}

static bool append_utf8(uint32_t codepoint, char *output, size_t capacity,
                        size_t *length)
{
    unsigned char encoded[4];
    size_t encoded_length;
    if (codepoint <= 0x7FU) {
        encoded[0] = (unsigned char)codepoint;
        encoded_length = 1U;
    } else if (codepoint <= 0x7FFU) {
        encoded[0] = (unsigned char)(0xC0U | (codepoint >> 6U));
        encoded[1] = (unsigned char)(0x80U | (codepoint & 0x3FU));
        encoded_length = 2U;
    } else if (codepoint <= 0xFFFFU) {
        encoded[0] = (unsigned char)(0xE0U | (codepoint >> 12U));
        encoded[1] = (unsigned char)(0x80U | ((codepoint >> 6U) & 0x3FU));
        encoded[2] = (unsigned char)(0x80U | (codepoint & 0x3FU));
        encoded_length = 3U;
    } else if (codepoint <= 0x10FFFFU) {
        encoded[0] = (unsigned char)(0xF0U | (codepoint >> 18U));
        encoded[1] = (unsigned char)(0x80U | ((codepoint >> 12U) & 0x3FU));
        encoded[2] = (unsigned char)(0x80U | ((codepoint >> 6U) & 0x3FU));
        encoded[3] = (unsigned char)(0x80U | (codepoint & 0x3FU));
        encoded_length = 4U;
    } else {
        return false;
    }
    if (encoded_length >= capacity - *length) return false;
    memcpy(output + *length, encoded, encoded_length);
    *length += encoded_length;
    return true;
}

static bool decode_json_string(const char *frame, size_t start, size_t end,
                               char *output, size_t output_size)
{
    size_t cursor = start;
    size_t output_length = 0U;
    if (output_size == 0U) return false;
    output[0] = '\0';
    while (cursor < end) {
        uint32_t codepoint;
        const unsigned char value = (unsigned char)frame[cursor++];
        if (value != '\\') {
            if (value < 0x20U) return false;
            if (value < 0x80U) {
                codepoint = value;
            } else {
                --cursor;
                if (!utf8_decode((const unsigned char *)frame, end, &cursor,
                                 &codepoint)) {
                    return false;
                }
            }
        } else {
            if (cursor >= end) return false;
            const unsigned char escape = (unsigned char)frame[cursor++];
            switch (escape) {
            case '"': codepoint = '"'; break;
            case '\\': codepoint = '\\'; break;
            case '/': codepoint = '/'; break;
            case 'b': codepoint = '\b'; break;
            case 'f': codepoint = '\f'; break;
            case 'n': codepoint = '\n'; break;
            case 'r': codepoint = '\r'; break;
            case 't': codepoint = '\t'; break;
            case 'u': {
                if (!json_hex4(frame, end, cursor, &codepoint)) return false;
                cursor += 4U;
                if (codepoint >= 0xD800U && codepoint <= 0xDBFFU) {
                    uint32_t low;
                    if (end - cursor < 6U || frame[cursor] != '\\' ||
                        frame[cursor + 1U] != 'u' ||
                        !json_hex4(frame, end, cursor + 2U, &low) ||
                        low < 0xDC00U || low > 0xDFFFU) {
                        return false;
                    }
                    cursor += 6U;
                    codepoint = 0x10000U +
                                ((codepoint - 0xD800U) << 10U) +
                                (low - 0xDC00U);
                } else if (codepoint >= 0xDC00U && codepoint <= 0xDFFFU) {
                    return false;
                }
                break;
            }
            default: return false;
            }
        }
        if (codepoint == 0U ||
            !append_utf8(codepoint, output, output_size, &output_length)) {
            return false;
        }
    }
    output[output_length] = '\0';
    return true;
}

static bool json_value_end(const char *frame, size_t length, size_t start,
                           size_t *end)
{
    size_t nested = 0U;
    for (size_t cursor = start; cursor < length; ++cursor) {
        const unsigned char value = (unsigned char)frame[cursor];
        if (value == '"') {
            size_t string_end;
            if (!json_string_end(frame, length, cursor, &string_end)) {
                return false;
            }
            cursor = string_end;
        } else if (value == '{' || value == '[') {
            ++nested;
        } else if (value == '}' || value == ']') {
            if (nested == 0U) {
                *end = cursor;
                return true;
            }
            --nested;
        } else if (value == ',' && nested == 0U) {
            *end = cursor;
            return true;
        }
    }
    *end = length;
    return true;
}

static bool redact_password_strings(const char *frame, size_t length,
                                    char *redacted, size_t *password_start,
                                    size_t *password_end,
                                    bool *password_found)
{
    memcpy(redacted, frame, length);
    redacted[length] = '\0';
    *password_found = false;
    size_t depth = 0U;
    for (size_t index = 0U; index < length; ++index) {
        const unsigned char value = (unsigned char)frame[index];
        if (value == '"') {
            size_t key_end;
            if (!json_string_end(frame, length, index, &key_end)) return false;
            size_t cursor = key_end + 1U;
            while (cursor < length &&
                   json_whitespace((unsigned char)frame[cursor])) {
                ++cursor;
            }
            if (cursor < length && frame[cursor] == ':' &&
                json_string_equals_ascii(frame, index + 1U, key_end,
                                         "password")) {
                ++cursor;
                while (cursor < length &&
                       json_whitespace((unsigned char)frame[cursor])) {
                    ++cursor;
                }
                if (cursor < length && frame[cursor] == '"') {
                    size_t value_end;
                    if (!json_string_end(frame, length, cursor, &value_end)) {
                        return false;
                    }
                    memset(redacted + cursor + 1U, 'x',
                           value_end - cursor - 1U);
                    if (depth == 1U) {
                        if (*password_found) return false;
                        *password_found = true;
                        *password_start = cursor + 1U;
                        *password_end = value_end;
                    }
                } else if (cursor < length) {
                    size_t value_end;
                    if (!json_value_end(frame, length, cursor, &value_end)) {
                        return false;
                    }
                    while (value_end > cursor &&
                           json_whitespace(
                               (unsigned char)frame[value_end - 1U])) {
                        --value_end;
                    }
                    if (value_end - cursor >= sizeof("null") - 1U) {
                        memset(redacted + cursor, ' ', value_end - cursor);
                        memcpy(redacted + cursor, "null",
                               sizeof("null") - 1U);
                    }
                }
            }
            index = key_end;
        } else if (value == '{' || value == '[') {
            ++depth;
        } else if (value == '}' || value == ']') {
            if (depth == 0U) return false;
            --depth;
        }
    }
    return true;
}

static raw_index_result_t parse_raw_index_value(const char *frame,
                                                size_t length, size_t start,
                                                uint32_t *output)
{
    size_t cursor = start;
    while (cursor < length && json_whitespace((unsigned char)frame[cursor])) {
        ++cursor;
    }
    if (cursor == length || frame[cursor] < '0' || frame[cursor] > '9') {
        return RAW_INDEX_INVALID;
    }

    uint32_t value = 0U;
    if (frame[cursor] == '0') {
        ++cursor;
        if (cursor < length && frame[cursor] >= '0' && frame[cursor] <= '9') {
            return RAW_INDEX_INVALID;
        }
    } else {
        while (cursor < length && frame[cursor] >= '0' && frame[cursor] <= '9') {
            const uint32_t digit = (uint32_t)(frame[cursor] - '0');
            if (value > (WEB_PROTOCOL_INDEX_MAX - digit) / 10U) {
                return RAW_INDEX_INVALID;
            }
            value = value * 10U + digit;
            ++cursor;
        }
    }

    while (cursor < length && json_whitespace((unsigned char)frame[cursor])) {
        ++cursor;
    }
    if (cursor == length || (frame[cursor] != ',' && frame[cursor] != '}')) {
        return RAW_INDEX_INVALID;
    }
    *output = value;
    return RAW_INDEX_VALID;
}

static raw_index_result_t find_top_level_raw_index(const char *frame,
                                                   size_t length,
                                                   uint32_t *output)
{
    size_t depth = 0U;
    raw_index_result_t result = RAW_INDEX_NOT_FOUND;
    for (size_t index = 0U; index < length; ++index) {
        const unsigned char value = (unsigned char)frame[index];
        if (value == '"') {
            size_t end;
            if (!json_string_end(frame, length, index, &end)) {
                return RAW_INDEX_INVALID;
            }
            if (depth == 1U &&
                json_string_equals_ascii(frame, index + 1U, end, "index")) {
                size_t cursor = end + 1U;
                while (cursor < length &&
                       json_whitespace((unsigned char)frame[cursor])) {
                    ++cursor;
                }
                if (cursor < length && frame[cursor] == ':') {
                    if (result != RAW_INDEX_NOT_FOUND) return RAW_INDEX_INVALID;
                    result = parse_raw_index_value(frame, length, cursor + 1U,
                                                   output);
                    if (result != RAW_INDEX_VALID) return result;
                }
            }
            index = end;
        } else if (value == '{' || value == '[') {
            ++depth;
        } else if (value == '}' || value == ']') {
            if (depth == 0U) return RAW_INDEX_INVALID;
            --depth;
        }
    }
    return result;
}

static bool text_valid(const char *text, size_t max_length, bool allow_empty)
{
    if (text == NULL) {
        return false;
    }
    const size_t length = strlen(text);
    return length <= max_length && (allow_empty || length > 0U) &&
           utf8_valid(text, length, true);
}

static bool request_id_valid(const char *request_id)
{
    if (request_id == NULL) return false;
    const size_t length = strlen(request_id);
    if (length == 0U || length > WEB_PROTOCOL_REQUEST_ID_MAX) return false;
    for (size_t index = 0U; index < length; ++index) {
        const unsigned char value = (unsigned char)request_id[index];
        const bool accepted = (value >= 'a' && value <= 'z') ||
                              (value >= 'A' && value <= 'Z') ||
                              (value >= '0' && value <= '9') || value == '.' ||
                              value == '_' || value == '-';
        if (!accepted) return false;
    }
    return true;
}

static uint32_t field_bit(const char *name)
{
    if (name == NULL) return 0U;
    if (strcmp(name, "type") == 0) return FIELD_TYPE;
    if (strcmp(name, "id") == 0) return FIELD_ID;
    if (strcmp(name, "action") == 0) return FIELD_ACTION;
    if (strcmp(name, "source") == 0) return FIELD_SOURCE;
    if (strcmp(name, "index") == 0) return FIELD_INDEX;
    if (strcmp(name, "ssid") == 0) return FIELD_SSID;
    if (strcmp(name, "password") == 0) return FIELD_PASSWORD;
    return 0U;
}

static bool collect_fields(const cJSON *root, uint32_t *fields)
{
    uint32_t seen = 0U;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, root) {
        if (!text_valid(item->string, 16U, false)) return false;
        const uint32_t bit = field_bit(item->string);
        if (bit == 0U || (seen & bit) != 0U) return false;
        seen |= bit;
    }
    *fields = seen;
    return true;
}

static const cJSON *object_item(const cJSON *root, const char *name)
{
    return cJSON_GetObjectItemCaseSensitive(root, name);
}

static void wipe_password_values_recursive(cJSON *item, size_t depth,
                                           size_t *remaining)
{
    for (; item != NULL && *remaining > 0U; item = item->next) {
        --*remaining;
        if (item->string != NULL && strcmp(item->string, "password") == 0 &&
            cJSON_IsString(item) && item->valuestring != NULL) {
            secure_zero(item->valuestring, strlen(item->valuestring));
        }
        if (item->child != NULL && depth < WEB_PROTOCOL_JSON_MAX_DEPTH) {
            wipe_password_values_recursive(item->child, depth + 1U,
                                           remaining);
        }
    }
}

static void wipe_password_values(cJSON *root)
{
    /* A 512-byte accepted frame cannot contain more than 512 JSON nodes. */
    size_t remaining = WEB_PROTOCOL_FRAME_MAX;
    wipe_password_values_recursive(root, 0U, &remaining);
}

static bool json_string(const cJSON *item, size_t max_length, bool allow_empty)
{
    return cJSON_IsString(item) &&
           text_valid(item->valuestring, max_length, allow_empty);
}

static bool parse_common(const cJSON *root, uint32_t fields,
                         web_command_t *parsed, const char **action)
{
    if ((fields & COMMON_FIELDS) != COMMON_FIELDS) return false;
    const cJSON *type = object_item(root, "type");
    const cJSON *id = object_item(root, "id");
    const cJSON *action_item = object_item(root, "action");
    if (!json_string(type, sizeof("command") - 1U, false) ||
        strcmp(type->valuestring, "command") != 0 ||
        !cJSON_IsString(id) || !request_id_valid(id->valuestring) ||
        !json_string(action_item, 32U, false)) {
        return false;
    }
    memcpy(parsed->request_id, id->valuestring, strlen(id->valuestring) + 1U);
    *action = action_item->valuestring;
    return true;
}

static bool parse_player_action(const cJSON *root, uint32_t fields,
                                const char *action,
                                raw_index_result_t raw_index_result,
                                uint32_t raw_index, web_command_t *parsed)
{
    parsed->kind = WEB_COMMAND_PLAYER;
    parsed->player.source = AUDIO_SOURCE_NONE;
    parsed->player.item_index = PLAYER_ITEM_NONE;

    if (strcmp(action, "player.play") == 0) {
        if (fields != COMMON_FIELDS) return false;
        parsed->player.kind = PLAYER_COMMAND_PLAY;
        return true;
    }
    if (strcmp(action, "player.pause") == 0) {
        if (fields != COMMON_FIELDS) return false;
        parsed->player.kind = PLAYER_COMMAND_PAUSE;
        return true;
    }
    if (strcmp(action, "player.toggle") == 0) {
        if (fields != COMMON_FIELDS) return false;
        parsed->player.kind = PLAYER_COMMAND_TOGGLE;
        return true;
    }
    if (strcmp(action, "source.select") == 0) {
        if (fields != (COMMON_FIELDS | FIELD_SOURCE)) return false;
        const cJSON *source = object_item(root, "source");
        if (!json_string(source, sizeof("internet_radio") - 1U, false)) return false;
        // Only the sources that are actually implemented are accepted; the
        // remaining audio_source_t values still name unbuilt modes.
        if (strcmp(source->valuestring, "internet_radio") == 0) {
            parsed->player.source = AUDIO_SOURCE_INTERNET_RADIO;
        } else if (strcmp(source->valuestring, "usb") == 0) {
            parsed->player.source = AUDIO_SOURCE_USB;
        } else {
            return false;
        }
        parsed->player.kind = PLAYER_COMMAND_SELECT_SOURCE;
        return true;
    }
    if (strcmp(action, "browse.up") == 0) {
        // No index: "up" is relative to whatever directory the shared listing
        // is on, which is the same one the device screen shows.
        if (fields != COMMON_FIELDS) return false;
        parsed->player.kind = PLAYER_COMMAND_BROWSE_UP;
        parsed->player.source = AUDIO_SOURCE_USB;
        return true;
    }
    if (strcmp(action, "list.select") == 0) {
        if (fields != (COMMON_FIELDS | FIELD_INDEX)) return false;
        const cJSON *index = object_item(root, "index");
        if (!cJSON_IsNumber(index) || raw_index_result != RAW_INDEX_VALID) return false;
        parsed->player.kind = PLAYER_COMMAND_SELECT_ITEM;
        parsed->player.item_index = (size_t)raw_index;
        return true;
    }
    return false;
}

static bool parse_wifi_action(const cJSON *root, uint32_t fields,
                              const char *action, web_command_t *parsed)
{
    if (strcmp(action, "wifi.save") != 0 ||
        fields != (COMMON_FIELDS | FIELD_SSID | FIELD_PASSWORD)) {
        return false;
    }
    const cJSON *ssid = object_item(root, "ssid");
    const cJSON *password = object_item(root, "password");
    if (!json_string(ssid, WIFI_SETTINGS_SSID_MAX_LEN, false) ||
        !cJSON_IsString(password)) {
        return false;
    }
    parsed->kind = WEB_COMMAND_WIFI_SAVE;
    memcpy(parsed->wifi.ssid, ssid->valuestring, strlen(ssid->valuestring) + 1U);
    return true;
}

web_protocol_result_t web_protocol_parse_command(const char *frame,
                                                  size_t frame_length,
                                                  web_command_t *command)
{
    if (command == NULL) return WEB_PROTOCOL_INVALID;
    web_protocol_clear_command(command);
    if (frame_length > WEB_PROTOCOL_FRAME_MAX) return WEB_PROTOCOL_TOO_LARGE;
    if (frame == NULL || frame_length == 0U ||
        memchr(frame, '\0', frame_length) != NULL ||
        !utf8_valid(frame, frame_length, false) ||
        !json_depth_safe(frame, frame_length) ||
        !json_has_no_escaped_nul(frame, frame_length)) {
        return WEB_PROTOCOL_INVALID;
    }

    char raw[WEB_PROTOCOL_FRAME_MAX + 1U];
    char redacted[WEB_PROTOCOL_FRAME_MAX + 1U];
    char decoded_password[WIFI_SETTINGS_PASSWORD_MAX_LEN + 1U] = {0};
    memcpy(raw, frame, frame_length);
    raw[frame_length] = '\0';
    size_t password_start = 0U;
    size_t password_end = 0U;
    bool password_found = false;
    if (!redact_password_strings(raw, frame_length, redacted,
                                 &password_start, &password_end,
                                 &password_found)) {
        secure_zero(raw, sizeof(raw));
        secure_zero(redacted, sizeof(redacted));
        secure_zero(decoded_password, sizeof(decoded_password));
        return WEB_PROTOCOL_INVALID;
    }

    const char *parse_end = NULL;
    bool exact = false;
    cJSON *root = parse_document(redacted, frame_length, &parse_end, &exact);
    if (!exact || !cJSON_IsObject(root)) {
        wipe_password_values(root);
        cJSON_Delete(root);
        secure_zero(raw, sizeof(raw));
        secure_zero(redacted, sizeof(redacted));
        secure_zero(decoded_password, sizeof(decoded_password));
        return WEB_PROTOCOL_INVALID;
    }

    web_command_t parsed = {0};
    uint32_t fields = 0U;
    uint32_t raw_index = 0U;
    const raw_index_result_t raw_index_result =
        find_top_level_raw_index(raw, frame_length, &raw_index);
    const char *action = NULL;
    bool valid = collect_fields(root, &fields) &&
                 parse_common(root, fields, &parsed, &action);
    if (valid) {
        valid = parse_player_action(root, fields, action, raw_index_result,
                                    raw_index, &parsed) ||
                parse_wifi_action(root, fields, action, &parsed);
    }
    if (valid && parsed.kind == WEB_COMMAND_WIFI_SAVE) {
        valid = password_found &&
                decode_json_string(raw, password_start, password_end,
                                   decoded_password,
                                   sizeof(decoded_password)) &&
                text_valid(decoded_password, WIFI_SETTINGS_PASSWORD_MAX_LEN,
                           true);
        if (valid) {
            memcpy(parsed.wifi.password, decoded_password,
                   strlen(decoded_password) + 1U);
        }
    }

    wipe_password_values(root);
    cJSON_Delete(root);
    secure_zero(raw, sizeof(raw));
    secure_zero(redacted, sizeof(redacted));
    secure_zero(decoded_password, sizeof(decoded_password));
    if (!valid) {
        web_protocol_clear_command(&parsed);
        return WEB_PROTOCOL_INVALID;
    }

    *command = parsed;
    web_protocol_clear_command(&parsed);
    return WEB_PROTOCOL_OK;
}
