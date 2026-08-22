#include "ui_player_state.h"

#include <string.h>

static void ui_player_state_derive_view(ui_player_state_t *state,
                                        const player_snapshot_t *snapshot)
{
    state->source = snapshot->active_source;
    state->view = snapshot->active_source == AUDIO_SOURCE_NONE
                      ? UI_PLAYER_VIEW_MENU
                      : UI_PLAYER_VIEW_SOURCE;
}

static bool ui_player_state_playback_is_active(player_playback_state_t state)
{
    return state == PLAYER_PLAYBACK_CONNECTING ||
           state == PLAYER_PLAYBACK_PLAYING ||
           state == PLAYER_PLAYBACK_PAUSED ||
           state == PLAYER_PLAYBACK_RECONNECTING;
}

static bool ui_player_state_snapshot_confirms(
    const ui_player_state_t *state, const player_snapshot_t *snapshot)
{
    switch (state->pending_command.kind) {
    case PLAYER_COMMAND_SELECT_SOURCE:
        return snapshot->active_source == state->pending_command.source;
    case PLAYER_COMMAND_STOP_SOURCE:
        return snapshot->active_source == AUDIO_SOURCE_NONE &&
               !state->pending_stop_requires_source_departure;
    case PLAYER_COMMAND_SELECT_ITEM:
        if (snapshot->active_source != AUDIO_SOURCE_INTERNET_RADIO ||
            snapshot->active_item_index != state->pending_command.item_index) {
            return false;
        }
        if (state->pending_command.item_index !=
            state->pending_origin_confirmed_item) {
            return true;
        }
        if (ui_player_state_playback_is_active(
                state->pending_origin_playback_state)) {
            return true;
        }
        return ui_player_state_playback_is_active(snapshot->playback_state);
    default:
        return true;
    }
}

void ui_player_state_init(ui_player_state_t *state)
{
    if (state == NULL) return;
    memset(state, 0, sizeof(*state));
    state->view = UI_PLAYER_VIEW_MENU;
    state->source = AUDIO_SOURCE_NONE;
    state->confirmed_source = AUDIO_SOURCE_NONE;
    state->confirmed_playback_state = PLAYER_PLAYBACK_STOPPED;
    state->confirmed_item_index = PLAYER_ITEM_NONE;
}

bool ui_player_state_can_select_item(const ui_player_state_t *state,
                                     size_t item_index)
{
    return state != NULL &&
           state->confirmed_source == AUDIO_SOURCE_INTERNET_RADIO &&
           item_index < state->item_count;
}

// USB browsing stays outside the pending machinery. That machinery exists for
// the radio, where switching stations blocks for seconds while the decoder
// task exits and the UI must not accept a second command meanwhile. Opening a
// directory changes the listing rather than the active item, so no snapshot
// could ever confirm it, and it would sit pending until the timeout.
static bool ui_player_state_is_files_browse(const player_command_t *command)
{
    return command->source == AUDIO_SOURCE_USB &&
           (command->kind == PLAYER_COMMAND_SELECT_ITEM ||
            command->kind == PLAYER_COMMAND_BROWSE_UP ||
            command->kind == PLAYER_COMMAND_BROWSE_REVEAL);
}

bool ui_player_state_can_post(const ui_player_state_t *state,
                              const player_command_t *command)
{
    if (state == NULL || command == NULL) return false;
    if (ui_player_state_is_files_browse(command)) return !state->pending;
    switch (command->kind) {
    case PLAYER_COMMAND_SELECT_SOURCE:
        return !state->pending && command->source != AUDIO_SOURCE_NONE;
    case PLAYER_COMMAND_STOP_SOURCE:
        return !state->pending ||
               state->pending_command.kind == PLAYER_COMMAND_SELECT_SOURCE ||
               state->pending_command.kind == PLAYER_COMMAND_SELECT_ITEM;
    case PLAYER_COMMAND_SELECT_ITEM:
        return !state->pending &&
               ui_player_state_can_select_item(state, command->item_index);
    case PLAYER_COMMAND_PLAY:
    case PLAYER_COMMAND_PAUSE:
    case PLAYER_COMMAND_TOGGLE:
    // A seek changes where the file is playing from, not what is playing, so
    // no snapshot field ever moves to confirm it - the same reason USB
    // browsing stays out of the pending machinery.
    case PLAYER_COMMAND_SEEK:
        return true;
    default:
        return false;
    }
}

bool ui_player_state_apply_post_result(ui_player_state_t *state,
                                       const player_command_t *command,
                                       bool posted, uint32_t now_ms)
{
    if (!posted || !ui_player_state_can_post(state, command)) return false;
    if (command->kind == PLAYER_COMMAND_PLAY ||
        command->kind == PLAYER_COMMAND_PAUSE ||
        command->kind == PLAYER_COMMAND_TOGGLE ||
        command->kind == PLAYER_COMMAND_SEEK ||
        ui_player_state_is_files_browse(command)) return true;

    const bool superseding_stop =
        command->kind == PLAYER_COMMAND_STOP_SOURCE && state->pending;

    state->pending = true;
    state->pending_command = *command;
    state->pending_stop_requires_source_departure =
        superseding_stop && state->confirmed_source == AUDIO_SOURCE_NONE;
    state->pending_started_ms = now_ms;
    if (command->kind == PLAYER_COMMAND_STOP_SOURCE) {
        state->pending_origin_view =
            state->confirmed_source == AUDIO_SOURCE_NONE
                ? UI_PLAYER_VIEW_MENU
                : UI_PLAYER_VIEW_SOURCE;
        state->pending_origin_source = state->confirmed_source;
    } else {
        state->pending_origin_view = state->view;
        state->pending_origin_source = state->source;
    }
    state->pending_origin_confirmed_source = state->confirmed_source;
    state->pending_origin_playback_state = state->confirmed_playback_state;
    state->pending_origin_confirmed_item = state->confirmed_item_index;

    if (command->kind == PLAYER_COMMAND_STOP_SOURCE) {
        state->view = UI_PLAYER_VIEW_MENU;
        state->source = AUDIO_SOURCE_NONE;
    } else {
        state->view = UI_PLAYER_VIEW_SOURCE;
        state->source = command->kind == PLAYER_COMMAND_SELECT_SOURCE
                            ? command->source
                            : AUDIO_SOURCE_INTERNET_RADIO;
    }
    return true;
}

void ui_player_state_apply_snapshot(ui_player_state_t *state,
                                    const player_snapshot_t *snapshot,
                                    uint32_t now_ms)
{
    if (state == NULL || snapshot == NULL) return;

    const bool first_snapshot = !state->have_snapshot;
    const audio_source_t previous_source = state->confirmed_source;
    const player_playback_state_t previous_playback =
        state->confirmed_playback_state;
    const size_t previous_item = state->confirmed_item_index;
    if (state->pending &&
        state->pending_command.kind == PLAYER_COMMAND_STOP_SOURCE &&
        state->pending_stop_requires_source_departure &&
        snapshot->active_source != AUDIO_SOURCE_NONE) {
        state->pending_stop_requires_source_departure = false;
    }
    const bool confirms_pending = state->pending &&
        ui_player_state_snapshot_confirms(state, snapshot);
    bool pending_can_still_complete = false;
    if (state->pending) {
        if (state->pending_command.kind == PLAYER_COMMAND_STOP_SOURCE) {
            pending_can_still_complete = true;
        } else {
            pending_can_still_complete =
                snapshot->active_source ==
                    state->pending_origin_confirmed_source;
        }
        if (state->pending_command.kind == PLAYER_COMMAND_SELECT_ITEM) {
            pending_can_still_complete = pending_can_still_complete &&
                (snapshot->active_item_index ==
                     state->pending_origin_confirmed_item ||
                 snapshot->active_item_index ==
                     state->pending_command.item_index ||
                 snapshot->active_item_index == PLAYER_ITEM_NONE);
        }
    }

    state->have_snapshot = true;
    state->confirmed_source = snapshot->active_source;
    state->confirmed_playback_state = snapshot->playback_state;
    state->confirmed_item_index = snapshot->active_item_index;
    state->item_count = snapshot->item_count;

    if (state->pending) {
        if (confirms_pending) {
            state->pending = false;
            state->source = snapshot->active_source;
            return;
        }
        if (!pending_can_still_complete) {
            state->pending = false;
            ui_player_state_derive_view(state, snapshot);
            return;
        }
        if ((uint32_t)(now_ms - state->pending_started_ms) >=
            UI_PLAYER_PENDING_TIMEOUT_MS) {
            state->pending = false;
            if (state->pending_command.kind == PLAYER_COMMAND_STOP_SOURCE) {
                ui_player_state_derive_view(state, snapshot);
            } else {
                state->view = state->pending_origin_view;
                state->source = state->pending_origin_source;
            }
        }
        return;
    }

    if (first_snapshot || snapshot->active_source != previous_source) {
        ui_player_state_derive_view(state, snapshot);
    } else if (state->view == UI_PLAYER_VIEW_STATION_LIST &&
               snapshot->active_source == AUDIO_SOURCE_INTERNET_RADIO &&
               ((snapshot->active_item_index != previous_item &&
                 snapshot->active_item_index < snapshot->item_count) ||
                (!ui_player_state_playback_is_active(previous_playback) &&
                 ui_player_state_playback_is_active(
                     snapshot->playback_state)))) {
        state->view = UI_PLAYER_VIEW_SOURCE;
        state->source = AUDIO_SOURCE_INTERNET_RADIO;
    }
}

bool ui_player_state_show_station_list(ui_player_state_t *state)
{
    // Both sources that have a list share this view: stations for the radio,
    // the current directory for USB.
    if (state == NULL || (state->source != AUDIO_SOURCE_INTERNET_RADIO &&
                          state->source != AUDIO_SOURCE_USB)) {
        return false;
    }
    state->view = UI_PLAYER_VIEW_STATION_LIST;
    if (state->pending) {
        // If a pending command times out later, it should revert to where the
        // user actually navigated to, not to the view captured when the
        // command was originally posted (which would silently discard this
        // navigation into the station list).
        state->pending_origin_view = UI_PLAYER_VIEW_STATION_LIST;
    }
    return true;
}

void ui_player_state_close_station_list(ui_player_state_t *state)
{
    if (state == NULL || state->view != UI_PLAYER_VIEW_STATION_LIST) return;
    state->view = UI_PLAYER_VIEW_SOURCE;
    if (state->pending) {
        // Mirror ui_player_state_show_station_list: if a pending command times
        // out later, it should revert to the source screen the idle timeout
        // just returned to, not to whatever view was captured when the
        // command was originally posted.
        state->pending_origin_view = UI_PLAYER_VIEW_SOURCE;
    }
}

ui_player_view_t ui_player_state_view(const ui_player_state_t *state)
{
    return state == NULL ? UI_PLAYER_VIEW_MENU : state->view;
}

audio_source_t ui_player_state_source(const ui_player_state_t *state)
{
    return state == NULL ? AUDIO_SOURCE_NONE : state->source;
}

size_t ui_player_state_active_item(const ui_player_state_t *state)
{
    return state == NULL ? PLAYER_ITEM_NONE : state->confirmed_item_index;
}

player_playback_state_t ui_player_state_playback(const ui_player_state_t *state)
{
    return state == NULL ? PLAYER_PLAYBACK_STOPPED : state->confirmed_playback_state;
}

bool ui_player_state_is_pending(const ui_player_state_t *state)
{
    return state != NULL && state->pending;
}

bool ui_player_state_pending_item(const ui_player_state_t *state, size_t *item_index)
{
    if (state == NULL || item_index == NULL || !state->pending ||
        state->pending_command.kind != PLAYER_COMMAND_SELECT_ITEM) {
        return false;
    }
    *item_index = state->pending_command.item_index;
    return true;
}
