#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "audio_source.h"
#include "icy_metadata.h"
#include "internet_radio_state.h"

#ifndef INTERNET_RADIO_TITLE_MAX_LEN
#define INTERNET_RADIO_TITLE_MAX_LEN ICY_METADATA_TITLE_MAX_LEN
#endif

#define PLAYER_NAME_MAX_LEN 64
#define PLAYER_TITLE_MAX_LEN INTERNET_RADIO_TITLE_MAX_LEN
#define PLAYER_CODEC_MAX_LEN 8
#define PLAYER_ERROR_MAX_LEN 64
#define PLAYER_ITEM_NONE SIZE_MAX
#define PLAYER_CAP_INTERNET_RADIO (1U << 0)

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PLAYER_PLAYBACK_STOPPED,
    PLAYER_PLAYBACK_CONNECTING,
    PLAYER_PLAYBACK_PLAYING,
    PLAYER_PLAYBACK_PAUSED,
    PLAYER_PLAYBACK_RECONNECTING,
    PLAYER_PLAYBACK_ERROR,
} player_playback_state_t;

typedef enum {
    PLAYER_COMMAND_SELECT_SOURCE,
    PLAYER_COMMAND_STOP_SOURCE,
    PLAYER_COMMAND_PLAY,
    PLAYER_COMMAND_PAUSE,
    PLAYER_COMMAND_TOGGLE,
    PLAYER_COMMAND_SELECT_ITEM,
} player_command_kind_t;

typedef struct {
    player_command_kind_t kind;
    audio_source_t source;
    size_t item_index;
} player_command_t;

typedef struct {
    uint32_t capabilities;
    audio_source_t active_source;
    player_playback_state_t playback_state;
    size_t active_item_index;
    size_t item_count;
    char context[PLAYER_NAME_MAX_LEN];
    char stream_title[PLAYER_TITLE_MAX_LEN];
    char codec[PLAYER_CODEC_MAX_LEN];
    uint16_t bitrate_kbps;
    uint32_t sample_rate_hz;
    bool wifi_rssi_valid;
    int8_t wifi_rssi_dbm;
    char error[PLAYER_ERROR_MAX_LEN];
} player_snapshot_t;

typedef enum {
    PLAYER_OPERATION_INVALID = -1,
    PLAYER_OPERATION_NONE,
    PLAYER_OPERATION_SELECT_SOURCE,
    PLAYER_OPERATION_STOP,
    PLAYER_OPERATION_START_SAVED,
    PLAYER_OPERATION_START_ITEM,
    PLAYER_OPERATION_PAUSE,
    PLAYER_OPERATION_RESUME,
} player_operation_t;

player_operation_t player_control_decide(const player_snapshot_t *state,
                                         const player_command_t *command);

#ifdef __cplusplus
}
#endif
