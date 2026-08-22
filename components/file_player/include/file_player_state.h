#pragma once

// Split out of file_player.h so player_control_logic.c can map this enum in the
// host build, where esp_err.h does not exist - the same split internet_radio
// makes with internet_radio_state.h.

typedef enum {
    FILE_PLAYER_STATE_STOPPED = 0,
    FILE_PLAYER_STATE_STARTING,
    FILE_PLAYER_STATE_PLAYING,
    FILE_PLAYER_STATE_PAUSED,
    FILE_PLAYER_STATE_ERROR,
} file_player_state_t;
