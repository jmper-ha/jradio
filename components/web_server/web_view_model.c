#include "web_view_model.h"

#include <stdbool.h>
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

static bool utf8_sequence(const unsigned char *value, size_t length,
                          size_t *consumed)
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
    while (continuation_count + 1U < expected &&
           continuation_count + 1U < length &&
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

static void copy_slice(const char *start, size_t length, char *output,
                       size_t output_size)
{
    if (output == NULL || output_size == 0U) return;
    static const unsigned char replacement[] = {0xEFU, 0xBFU, 0xBDU};
    size_t input_offset = 0U;
    size_t output_offset = 0U;
    while (input_offset < length) {
        size_t consumed;
        const bool valid = utf8_sequence(
            (const unsigned char *)start + input_offset,
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

static bool trimmed_equal(const char *left, const char *right)
{
    if (left == NULL || right == NULL) return false;
    size_t left_length = strlen(left);
    size_t right_length = strlen(right);
    trim_slice(&left, &left_length);
    trim_slice(&right, &right_length);
    return left_length == right_length &&
           memcmp(left, right, left_length) == 0;
}

void web_view_split_player_title(const char *stream_title,
                                 const char *configured_station_name,
                                 char *artist, size_t artist_size,
                                 char *title, size_t title_size)
{
    if (artist != NULL && artist_size > 0U) artist[0] = '\0';
    if (title != NULL && title_size > 0U) title[0] = '\0';
    if (stream_title == NULL) return;

    const char *whole = stream_title;
    size_t whole_length = strlen(stream_title);
    trim_slice(&whole, &whole_length);
    if (configured_station_name != NULL &&
        trimmed_equal(stream_title, configured_station_name)) {
        copy_slice(whole, whole_length, title, title_size);
        return;
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
        return;
    }

    const char *artist_start = whole;
    size_t artist_length = (size_t)(separator - whole);
    const char *title_start = separator + separator_length;
    size_t title_length = title_start <= whole_end ? (size_t)(whole_end - title_start) : 0U;
    trim_slice(&artist_start, &artist_length);
    trim_slice(&title_start, &title_length);
    if (artist_length == 0U || title_length == 0U) {
        copy_slice(whole, whole_length, title, title_size);
        return;
    }
    copy_slice(artist_start, artist_length, artist, artist_size);
    copy_slice(title_start, title_length, title, title_size);
}

void web_view_split_title(const char *stream_title,
                          char *artist, size_t artist_size,
                          char *title, size_t title_size)
{
    web_view_split_player_title(stream_title, NULL, artist, artist_size,
                                title, title_size);
}

const char *web_view_playback_name(player_playback_state_t state)
{
    switch (state) {
    case PLAYER_PLAYBACK_STOPPED: return "stopped";
    case PLAYER_PLAYBACK_CONNECTING: return "connecting";
    case PLAYER_PLAYBACK_PLAYING: return "playing";
    case PLAYER_PLAYBACK_PAUSED: return "paused";
    case PLAYER_PLAYBACK_RECONNECTING: return "reconnecting";
    case PLAYER_PLAYBACK_ERROR: return "error";
    default: return "unknown";
    }
}

const char *web_view_playback_label(player_playback_state_t state)
{
    switch (state) {
    case PLAYER_PLAYBACK_STOPPED: return "Остановлено";
    case PLAYER_PLAYBACK_CONNECTING: return "Подключение";
    case PLAYER_PLAYBACK_PLAYING: return "Воспроизведение";
    case PLAYER_PLAYBACK_PAUSED: return "Пауза";
    case PLAYER_PLAYBACK_RECONNECTING: return "Переподключение";
    case PLAYER_PLAYBACK_ERROR: return "Ошибка";
    default: return "Неизвестное состояние";
    }
}

const char *web_view_source_name(audio_source_t source)
{
    switch (source) {
    case AUDIO_SOURCE_NONE: return "none";
    case AUDIO_SOURCE_USB: return "usb";
    case AUDIO_SOURCE_INTERNET_RADIO: return "internet_radio";
    case AUDIO_SOURCE_DLNA: return "dlna";
    case AUDIO_SOURCE_FM: return "fm";
    case AUDIO_SOURCE_BLUETOOTH: return "bluetooth";
    default: return "unknown";
    }
}

const char *web_view_source_label(audio_source_t source)
{
    switch (source) {
    case AUDIO_SOURCE_NONE: return "Нет источника";
    case AUDIO_SOURCE_USB: return "USB-плеер";
    case AUDIO_SOURCE_INTERNET_RADIO: return "Интернет-радио";
    case AUDIO_SOURCE_DLNA: return "DLNA";
    case AUDIO_SOURCE_FM: return "FM-радио";
    case AUDIO_SOURCE_BLUETOOTH: return "Bluetooth";
    default: return "Неизвестный режим";
    }
}

uint32_t web_view_snapshot_changes(const player_snapshot_t *previous,
                                   const player_snapshot_t *current)
{
    if (previous == current) return WEB_VIEW_SECTION_NONE;
    if (previous == NULL || current == NULL) return WEB_VIEW_SECTION_ALL;

    uint32_t changes = WEB_VIEW_SECTION_NONE;
    if (previous->capabilities != current->capabilities) {
        changes |= WEB_VIEW_SECTION_CAPABILITIES;
    }
    if (previous->active_source != current->active_source) {
        changes |= WEB_VIEW_SECTION_PLAYER | WEB_VIEW_SECTION_LIST;
    }
    if (previous->playback_state != current->playback_state ||
        memcmp(previous->context, current->context, sizeof(previous->context)) != 0 ||
        memcmp(previous->stream_title, current->stream_title,
               sizeof(previous->stream_title)) != 0 ||
        memcmp(previous->codec, current->codec, sizeof(previous->codec)) != 0 ||
        previous->bitrate_kbps != current->bitrate_kbps ||
        previous->wifi_rssi_valid != current->wifi_rssi_valid ||
        previous->wifi_rssi_dbm != current->wifi_rssi_dbm ||
        memcmp(previous->error, current->error, sizeof(previous->error)) != 0) {
        changes |= WEB_VIEW_SECTION_PLAYER;
    }
    if (previous->active_item_index != current->active_item_index ||
        previous->item_count != current->item_count) {
        changes |= WEB_VIEW_SECTION_LIST;
    }
    return changes;
}
