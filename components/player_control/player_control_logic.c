#include "player_control.h"

#include <string.h>

#define PLAYER_RSSI_REFRESH_MS 1000U

bool player_rssi_refresh_due(bool seen, uint32_t last_update_ms,
                             uint32_t now_ms)
{
    return !seen ||
           (uint32_t)(now_ms - last_update_ms) >= PLAYER_RSSI_REFRESH_MS;
}

player_playback_state_t player_playback_from_radio(internet_radio_state_t state)
{
    switch (state) {
    case INTERNET_RADIO_STATE_STOPPED:
        return PLAYER_PLAYBACK_STOPPED;
    case INTERNET_RADIO_STATE_CONNECTING:
        return PLAYER_PLAYBACK_CONNECTING;
    case INTERNET_RADIO_STATE_PLAYING:
        return PLAYER_PLAYBACK_PLAYING;
    case INTERNET_RADIO_STATE_PAUSED:
        return PLAYER_PLAYBACK_PAUSED;
    case INTERNET_RADIO_STATE_RECONNECTING:
        return PLAYER_PLAYBACK_RECONNECTING;
    case INTERNET_RADIO_STATE_ERROR:
        return PLAYER_PLAYBACK_ERROR;
    default:
        return PLAYER_PLAYBACK_ERROR;
    }
}

bool player_snapshot_equal(const player_snapshot_t *left,
                           const player_snapshot_t *right)
{
    if (left == right) {
        return true;
    }
    if (left == NULL || right == NULL) {
        return false;
    }
    return left->capabilities == right->capabilities &&
           left->active_source == right->active_source &&
           left->playback_state == right->playback_state &&
           left->active_item_index == right->active_item_index &&
           left->item_count == right->item_count &&
           memcmp(left->context, right->context, sizeof(left->context)) == 0 &&
           memcmp(left->stream_title, right->stream_title,
                  sizeof(left->stream_title)) == 0 &&
           memcmp(left->codec, right->codec, sizeof(left->codec)) == 0 &&
           left->bitrate_kbps == right->bitrate_kbps &&
           left->wifi_rssi_valid == right->wifi_rssi_valid &&
           left->wifi_rssi_dbm == right->wifi_rssi_dbm &&
           memcmp(left->error, right->error, sizeof(left->error)) == 0;
}

player_operation_t player_control_decide(const player_snapshot_t *state,
                                         const player_command_t *command)
{
    if (state == NULL || command == NULL) return PLAYER_OPERATION_INVALID;
    switch (command->kind) {
    case PLAYER_COMMAND_SELECT_SOURCE: {
        // Unlike a plain no-op re-select, a source stuck in ERROR must stay
        // retryable: re-selecting it should restart it, not be swallowed.
        const bool already_healthy = command->source == state->active_source &&
            state->playback_state != PLAYER_PLAYBACK_ERROR;
        if (already_healthy) return PLAYER_OPERATION_NONE;
        return command->source == AUDIO_SOURCE_INTERNET_RADIO &&
                       (state->capabilities & PLAYER_CAP_INTERNET_RADIO) != 0U
                   ? PLAYER_OPERATION_SELECT_SOURCE : PLAYER_OPERATION_INVALID;
    }
    case PLAYER_COMMAND_STOP_SOURCE:
        return state->active_source == AUDIO_SOURCE_NONE ? PLAYER_OPERATION_NONE
                                                          : PLAYER_OPERATION_STOP;
    case PLAYER_COMMAND_PLAY:
        if (state->playback_state == PLAYER_PLAYBACK_PAUSED) return PLAYER_OPERATION_RESUME;
        if (state->playback_state == PLAYER_PLAYBACK_STOPPED ||
            state->playback_state == PLAYER_PLAYBACK_ERROR) return PLAYER_OPERATION_START_SAVED;
        return state->playback_state == PLAYER_PLAYBACK_PLAYING ||
                       state->playback_state == PLAYER_PLAYBACK_CONNECTING ||
                       state->playback_state == PLAYER_PLAYBACK_RECONNECTING
                   ? PLAYER_OPERATION_NONE : PLAYER_OPERATION_INVALID;
    case PLAYER_COMMAND_PAUSE:
        if (state->playback_state == PLAYER_PLAYBACK_PLAYING) return PLAYER_OPERATION_PAUSE;
        return state->playback_state == PLAYER_PLAYBACK_PAUSED ? PLAYER_OPERATION_NONE
                                                                : PLAYER_OPERATION_INVALID;
    case PLAYER_COMMAND_TOGGLE:
        if (state->playback_state == PLAYER_PLAYBACK_PLAYING) return PLAYER_OPERATION_PAUSE;
        if (state->playback_state == PLAYER_PLAYBACK_PAUSED) return PLAYER_OPERATION_RESUME;
        if (state->playback_state == PLAYER_PLAYBACK_STOPPED ||
            state->playback_state == PLAYER_PLAYBACK_ERROR) return PLAYER_OPERATION_START_SAVED;
        return state->playback_state == PLAYER_PLAYBACK_CONNECTING ||
                       state->playback_state == PLAYER_PLAYBACK_RECONNECTING
                   ? PLAYER_OPERATION_NONE : PLAYER_OPERATION_INVALID;
    case PLAYER_COMMAND_SELECT_ITEM: {
        if (state->active_source != AUDIO_SOURCE_INTERNET_RADIO ||
            command->item_index >= state->item_count) return PLAYER_OPERATION_INVALID;
        const bool already_healthy = command->item_index == state->active_item_index &&
            (state->playback_state == PLAYER_PLAYBACK_CONNECTING ||
             state->playback_state == PLAYER_PLAYBACK_PLAYING ||
             state->playback_state == PLAYER_PLAYBACK_PAUSED ||
             state->playback_state == PLAYER_PLAYBACK_RECONNECTING);
        return already_healthy ? PLAYER_OPERATION_NONE : PLAYER_OPERATION_START_ITEM;
    }
    default:
        return PLAYER_OPERATION_INVALID;
    }
}
