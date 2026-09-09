#include "web_view_model.h"

#include <string.h>

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

const char *web_view_playback_label(player_playback_state_t state,
                                    device_language_t language)
{
    device_text_id_t id;
    switch (state) {
    case PLAYER_PLAYBACK_STOPPED: id = DEVICE_TEXT_STATE_STOPPED; break;
    case PLAYER_PLAYBACK_CONNECTING: id = DEVICE_TEXT_STATE_CONNECTING; break;
    case PLAYER_PLAYBACK_PLAYING: id = DEVICE_TEXT_STATE_PLAYING; break;
    case PLAYER_PLAYBACK_PAUSED: id = DEVICE_TEXT_STATE_PAUSED; break;
    case PLAYER_PLAYBACK_RECONNECTING: id = DEVICE_TEXT_STATE_RECONNECTING; break;
    case PLAYER_PLAYBACK_ERROR: id = DEVICE_TEXT_STATE_ERROR; break;
    default: id = DEVICE_TEXT_STATE_UNKNOWN; break;
    }
    return device_text(id, language);
}

const char *web_view_source_name(audio_source_t source)
{
    switch (source) {
    case AUDIO_SOURCE_NONE: return "none";
    case AUDIO_SOURCE_USB: return "usb";
    case AUDIO_SOURCE_SD: return "sd";
    case AUDIO_SOURCE_INTERNET_RADIO: return "internet_radio";
    case AUDIO_SOURCE_YANDEX: return "yandex";
    case AUDIO_SOURCE_DLNA: return "dlna";
    case AUDIO_SOURCE_FM: return "fm";
    case AUDIO_SOURCE_BLUETOOTH: return "bluetooth";
    default: return "unknown";
    }
}

const char *web_view_source_label(audio_source_t source, device_language_t language)
{
    device_text_id_t id;
    switch (source) {
    case AUDIO_SOURCE_NONE: id = DEVICE_TEXT_SOURCE_NONE; break;
    case AUDIO_SOURCE_USB: id = DEVICE_TEXT_SOURCE_USB; break;
    case AUDIO_SOURCE_SD: id = DEVICE_TEXT_SOURCE_SD; break;
    case AUDIO_SOURCE_INTERNET_RADIO: id = DEVICE_TEXT_SOURCE_INTERNET_RADIO; break;
    case AUDIO_SOURCE_YANDEX: id = DEVICE_TEXT_SOURCE_YANDEX; break;
    case AUDIO_SOURCE_DLNA: id = DEVICE_TEXT_SOURCE_DLNA; break;
    case AUDIO_SOURCE_FM: id = DEVICE_TEXT_SOURCE_FM; break;
    case AUDIO_SOURCE_BLUETOOTH: id = DEVICE_TEXT_SOURCE_BLUETOOTH; break;
    default: id = DEVICE_TEXT_SOURCE_UNKNOWN; break;
    }
    return device_text(id, language);
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
        previous->sample_rate_hz != current->sample_rate_hz ||
        previous->wifi_rssi_valid != current->wifi_rssi_valid ||
        previous->wifi_rssi_dbm != current->wifi_rssi_dbm ||
        previous->track_likeable != current->track_likeable ||
        previous->track_liked != current->track_liked ||
        previous->track_disliked != current->track_disliked ||
        /* Not part of the section's own text, but what decides it: the three
         * lines the frame carries come out of the tags, and their arrival
         * changes nothing else in the snapshot. */
        previous->track_tag_revision != current->track_tag_revision ||
        memcmp(previous->error, current->error, sizeof(previous->error)) != 0) {
        changes |= WEB_VIEW_SECTION_PLAYER;
    }
    if (previous->active_item_index != current->active_item_index ||
        previous->item_count != current->item_count ||
        // Opening a sibling directory with the same number of entries changes
        // neither of the above, yet the list is entirely different.
        previous->listing_revision != current->listing_revision) {
        changes |= WEB_VIEW_SECTION_LIST;
    }
    return changes;
}
