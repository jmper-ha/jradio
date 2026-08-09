#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "player_control_types.h"

/* Station switch/stop can wait up to 12 s for the decoder task to exit. */
#define UI_PLAYER_PENDING_TIMEOUT_MS 13000U

typedef enum {
    UI_PLAYER_VIEW_MENU,
    UI_PLAYER_VIEW_SOURCE,
    UI_PLAYER_VIEW_STATION_LIST,
} ui_player_view_t;

typedef struct {
    ui_player_view_t view;
    audio_source_t source;
    bool have_snapshot;
    audio_source_t confirmed_source;
    player_playback_state_t confirmed_playback_state;
    size_t confirmed_item_index;
    size_t item_count;
    bool pending;
    player_command_t pending_command;
    bool pending_stop_requires_source_departure;
    uint32_t pending_started_ms;
    ui_player_view_t pending_origin_view;
    audio_source_t pending_origin_source;
    audio_source_t pending_origin_confirmed_source;
    player_playback_state_t pending_origin_playback_state;
    size_t pending_origin_confirmed_item;
} ui_player_state_t;

void ui_player_state_init(ui_player_state_t *state);
bool ui_player_state_can_post(const ui_player_state_t *state,
                              const player_command_t *command);
bool ui_player_state_apply_post_result(ui_player_state_t *state,
                                       const player_command_t *command,
                                       bool posted, uint32_t now_ms);
void ui_player_state_apply_snapshot(ui_player_state_t *state,
                                    const player_snapshot_t *snapshot,
                                    uint32_t now_ms);
bool ui_player_state_show_station_list(ui_player_state_t *state);
void ui_player_state_close_station_list(ui_player_state_t *state);
bool ui_player_state_can_select_item(const ui_player_state_t *state,
                                     size_t item_index);
ui_player_view_t ui_player_state_view(const ui_player_state_t *state);
audio_source_t ui_player_state_source(const ui_player_state_t *state);
size_t ui_player_state_active_item(const ui_player_state_t *state);
bool ui_player_state_is_pending(const ui_player_state_t *state);
bool ui_player_state_pending_item(const ui_player_state_t *state, size_t *item_index);
