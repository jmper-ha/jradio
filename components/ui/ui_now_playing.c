#include "ui_now_playing.h"

#include <stdint.h>
#include <string.h>

static bool trim_byte(unsigned char value)
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n' ||
           value == '\f' || value == '\v';
}

static void trim_slice(const char **start, size_t *length)
{
    while (*length > 0U && trim_byte((unsigned char)(*start)[0])) {
        ++*start;
        --*length;
    }
    while (*length > 0U && trim_byte((unsigned char)(*start)[*length - 1U])) {
        --*length;
    }
}

/* Whether the sequence at `value` is one whole, legal UTF-8 character, and how
 * many bytes were looked at either way. Overlong forms, surrogates and values
 * past U+10FFFF are refused: they are the shapes a decoder elsewhere might
 * read differently from this one. */
static bool utf8_sequence(const unsigned char *value, size_t length, size_t *consumed)
{
    const unsigned char first = value[0];
    if (first < 0x80U) {
        *consumed = 1U;
        return true;
    }

    size_t expected;
    uint32_t codepoint;
    uint32_t minimum;
    if (first >= 0xC0U && first <= 0xDFU) {
        expected = 2U;
        codepoint = first & 0x1FU;
        minimum = 0x80U;
    } else if (first >= 0xE0U && first <= 0xEFU) {
        expected = 3U;
        codepoint = first & 0x0FU;
        minimum = 0x800U;
    } else if (first >= 0xF0U && first <= 0xF7U) {
        expected = 4U;
        codepoint = first & 0x07U;
        minimum = 0x10000U;
    } else {
        *consumed = 1U;
        return false;
    }

    size_t continuation_count = 0U;
    while (continuation_count + 1U < expected && continuation_count + 1U < length &&
           (value[continuation_count + 1U] & 0xC0U) == 0x80U) {
        ++continuation_count;
    }
    *consumed = 1U + continuation_count;
    if (*consumed != expected) return false;

    for (size_t index = 1U; index < expected; ++index) {
        codepoint = (codepoint << 6) | (uint32_t)(value[index] & 0x3FU);
    }
    return codepoint >= minimum && codepoint <= 0x10FFFFU &&
           !(codepoint >= 0xD800U && codepoint <= 0xDFFFU);
}

static void copy_slice(const char *start, size_t length, char *output, size_t output_size)
{
    if (output == NULL || output_size == 0U) return;
    static const unsigned char replacement[] = {0xEFU, 0xBFU, 0xBDU};
    size_t input_offset = 0U;
    size_t output_offset = 0U;
    while (input_offset < length) {
        size_t consumed;
        const bool valid = utf8_sequence((const unsigned char *)start + input_offset,
                                         length - input_offset, &consumed);
        const size_t produced = valid ? consumed : sizeof(replacement);
        if (produced > output_size - 1U - output_offset) break;
        if (valid) {
            memcpy(output + output_offset, start + input_offset, consumed);
        } else {
            memcpy(output + output_offset, replacement, sizeof(replacement));
        }
        input_offset += consumed;
        output_offset += produced;
    }
    output[output_offset] = '\0';
}

static void copy_string(const char *text, char *output, size_t output_size)
{
    if (text == NULL) {
        if (output != NULL && output_size > 0U) output[0] = '\0';
        return;
    }
    copy_slice(text, strlen(text), output, output_size);
}

static bool trimmed_equal(const char *left, const char *right)
{
    if (left == NULL || right == NULL) return false;
    size_t left_length = strlen(left);
    size_t right_length = strlen(right);
    trim_slice(&left, &left_length);
    trim_slice(&right, &right_length);
    return left_length == right_length && memcmp(left, right, left_length) == 0;
}

const char *ui_now_playing_station_name(bool name_from_list, const char *list_name,
                                        const char *stream_name)
{
    if (list_name == NULL) list_name = "";
    if (stream_name == NULL) stream_name = "";
    if (name_from_list || stream_name[0] == '\0') return list_name;
    return stream_name;
}

bool ui_now_playing_split_title(const char *icy, const char *heading, char *artist,
                                size_t artist_size, char *title, size_t title_size)
{
    if (artist != NULL && artist_size > 0U) artist[0] = '\0';
    if (title != NULL && title_size > 0U) title[0] = '\0';
    if (icy == NULL) return false;

    const char *whole = icy;
    size_t whole_length = strlen(icy);
    trim_slice(&whole, &whole_length);
    if (whole_length == 0U) return false;
    if (trimmed_equal(icy, heading)) {
        copy_slice(whole, whole_length, title, title_size);
        return false;
    }

    static const char ascii_separator[] = " - ";
    static const char en_dash_separator[] = " \xE2\x80\x93 ";
    const char *ascii = strstr(whole, ascii_separator);
    const char *en_dash = strstr(whole, en_dash_separator);
    const char *separator = ascii;
    size_t separator_length = sizeof(ascii_separator) - 1U;
    if (separator == NULL || (en_dash != NULL && en_dash < separator)) {
        separator = en_dash;
        separator_length = sizeof(en_dash_separator) - 1U;
    }

    const char *whole_end = whole + whole_length;
    if (separator == NULL || separator >= whole_end) {
        copy_slice(whole, whole_length, title, title_size);
        return false;
    }

    const char *artist_start = whole;
    size_t artist_length = (size_t)(separator - whole);
    const char *title_start = separator + separator_length;
    size_t title_length =
        title_start <= whole_end ? (size_t)(whole_end - title_start) : 0U;
    trim_slice(&artist_start, &artist_length);
    trim_slice(&title_start, &title_length);
    if (artist_length == 0U || title_length == 0U) {
        copy_slice(whole, whole_length, title, title_size);
        return false;
    }
    /* The performer alone not fitting is the one case where splitting makes
     * things worse: half a name reads as a different performer, while the
     * whole string at least reads as what the station sent. */
    if (artist == NULL || artist_length >= artist_size) {
        copy_slice(whole, whole_length, title, title_size);
        return false;
    }
    copy_slice(artist_start, artist_length, artist, artist_size);
    copy_slice(title_start, title_length, title, title_size);
    return true;
}

void ui_now_playing_for_file(const char *directory, const char *file_name,
                             const audio_tags_t *tags, ui_now_playing_t *out)
{
    if (out == NULL) return;
    memset(out, 0, sizeof(*out));
    const bool tagged = tags != NULL;

    copy_string(tagged && tags->album[0] != '\0' ? tags->album : directory, out->heading,
                sizeof(out->heading));
    copy_string(tagged && tags->title[0] != '\0' ? tags->title : file_name, out->title,
                sizeof(out->title));
    copy_string(tagged ? tags->artist : "", out->artist, sizeof(out->artist));
}

void ui_now_playing_for_station(bool name_from_list, const char *list_name,
                                const char *stream_name, const char *icy_title,
                                ui_now_playing_t *out)
{
    if (out == NULL) return;
    memset(out, 0, sizeof(*out));

    copy_string(ui_now_playing_station_name(name_from_list, list_name, stream_name),
                out->heading, sizeof(out->heading));
    (void)ui_now_playing_split_title(icy_title, out->heading, out->artist,
                                     sizeof(out->artist), out->title, sizeof(out->title));
}
