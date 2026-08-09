#include "player_control_types.h"

player_operation_t player_control_decide(const player_snapshot_t *state,
                                         const player_command_t *command)
{
    if (state == NULL || command == NULL) return PLAYER_OPERATION_INVALID;
    switch (command->kind) {
    case PLAYER_COMMAND_SELECT_SOURCE:
        if (command->source == state->active_source) return PLAYER_OPERATION_NONE;
        return command->source == AUDIO_SOURCE_INTERNET_RADIO &&
                       (state->capabilities & PLAYER_CAP_INTERNET_RADIO) != 0U
                   ? PLAYER_OPERATION_SELECT_SOURCE : PLAYER_OPERATION_INVALID;
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
