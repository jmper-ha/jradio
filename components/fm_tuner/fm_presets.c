#include "fm_presets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rda5807.h"

static bool is_space(char c)
{
    return c == ' ' || c == '\r';
}

static void trim(const char **text, size_t *length)
{
    while (*length > 0U && is_space(**text)) {
        ++*text;
        --*length;
    }
    while (*length > 0U && is_space((*text)[*length - 1U])) --*length;
}

/* A picture's file name: letters, digits, dot, dash, underscore, and no
 * leading dot - anything else is a way out of radio_img. The rule the
 * playlist's pictures follow. */
static bool icon_is_valid(const char *text, size_t length)
{
    if (length == 0U) return true;
    if (text[0] == '.') return false;
    for (size_t i = 0U; i < length; ++i) {
        const char c = text[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
        if (!ok) return false;
    }
    return true;
}

static bool parse_line(const char *line, size_t length, fm_preset_t *preset)
{
    const char *first_tab = memchr(line, '\t', length);
    if (first_tab == NULL) return false;
    const char *fields[3] = {line, first_tab + 1, NULL};
    size_t lengths[3] = {(size_t)(first_tab - line), 0U, 0U};
    const size_t rest = length - lengths[0] - 1U;
    const char *second_tab = memchr(fields[1], '\t', rest);
    lengths[1] = second_tab != NULL ? (size_t)(second_tab - fields[1]) : rest;
    size_t count = 2U;
    if (second_tab != NULL) {
        fields[2] = second_tab + 1;
        lengths[2] = rest - lengths[1] - 1U;
        count = 3U;
    }
    for (size_t i = 0U; i < count; ++i) trim(&fields[i], &lengths[i]);

    char number[12];
    if (lengths[1] == 0U || lengths[1] >= sizeof(number)) return false;
    memcpy(number, fields[1], lengths[1]);
    number[lengths[1]] = '\0';
    char *end = NULL;
    const unsigned long khz = strtoul(number, &end, 10);
    if (end == number || *end != '\0' || khz < RDA5807_BAND_MIN_KHZ ||
        khz > RDA5807_BAND_MAX_KHZ) {
        return false;
    }
    if (count == 3U && (lengths[2] >= FM_PRESET_ICON_MAX_LEN ||
                        !icon_is_valid(fields[2], lengths[2]))) {
        return false;
    }

    memset(preset, 0, sizeof(*preset));
    // A long name is cut rather than refused: the station is still the station.
    size_t name_length = lengths[0] < FM_PRESET_NAME_MAX_LEN - 1U ? lengths[0]
                                                                 : FM_PRESET_NAME_MAX_LEN - 1U;
    // And never in the middle of a UTF-8 character.
    while (name_length > 0U && name_length < lengths[0] &&
           ((unsigned char)fields[0][name_length] & 0xC0U) == 0x80U) {
        --name_length;
    }
    memcpy(preset->name, fields[0], name_length);
    preset->khz = (uint32_t)khz;
    if (count == 3U) memcpy(preset->icon, fields[2], lengths[2]);
    return true;
}

size_t fm_presets_parse(const char *text, size_t length, fm_presets_t *presets)
{
    if (presets == NULL) return 0U;
    memset(presets, 0, sizeof(*presets));
    if (text == NULL) return 0U;
    size_t skipped = 0U;
    size_t at = 0U;
    while (at < length) {
        size_t end = at;
        while (end < length && text[end] != '\n') ++end;
        const char *line = text + at;
        size_t line_length = end - at;
        at = end + 1U;
        trim(&line, &line_length);
        if (line_length == 0U || line[0] == '#') continue;
        if (presets->count >= FM_PRESETS_MAX) {
            ++skipped;
            continue;
        }
        if (parse_line(line, line_length, &presets->presets[presets->count])) {
            ++presets->count;
        } else {
            ++skipped;
        }
    }
    return skipped;
}

size_t fm_presets_write(const fm_presets_t *presets, char *out, size_t capacity)
{
    if (out != NULL && capacity > 0U) out[0] = '\0';
    if (presets == NULL) return 0U;
    size_t used = 0U;
    for (size_t i = 0U; i < presets->count; ++i) {
        const fm_preset_t *preset = &presets->presets[i];
        char scratch[1];
        char *at = out != NULL && used < capacity ? out + used : scratch;
        const size_t room = out != NULL && used < capacity ? capacity - used : 0U;
        const int written =
            preset->icon[0] != '\0'
                ? snprintf(at, room, "%s\t%lu\t%s\n", preset->name, (unsigned long)preset->khz,
                           preset->icon)
                : snprintf(at, room, "%s\t%lu\n", preset->name, (unsigned long)preset->khz);
        if (written > 0) used += (size_t)written;
    }
    return used;
}

bool fm_presets_find(const fm_presets_t *presets, uint32_t khz, size_t *index)
{
    if (presets == NULL) return false;
    for (size_t i = 0U; i < presets->count; ++i) {
        if (presets->presets[i].khz == khz) {
            if (index != NULL) *index = i;
            return true;
        }
    }
    return false;
}
