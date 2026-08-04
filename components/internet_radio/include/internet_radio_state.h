#pragma once

#include <stdbool.h>

typedef enum {
    INTERNET_RADIO_STATE_STOPPED = 0,
    INTERNET_RADIO_STATE_CONNECTING,
    INTERNET_RADIO_STATE_PLAYING,
    INTERNET_RADIO_STATE_PAUSED,
    INTERNET_RADIO_STATE_RECONNECTING,
    INTERNET_RADIO_STATE_ERROR,
} internet_radio_state_t;

typedef enum {
    INTERNET_RADIO_EVENT_START = 0,
    INTERNET_RADIO_EVENT_PLAYER_RUNNING,
    INTERNET_RADIO_EVENT_PAUSE,
    INTERNET_RADIO_EVENT_RESUME,
    INTERNET_RADIO_EVENT_STOP,
    INTERNET_RADIO_EVENT_FAILURE,
    INTERNET_RADIO_EVENT_RETRY,
    INTERNET_RADIO_EVENT_FATAL,
} internet_radio_event_t;

bool internet_radio_state_apply(internet_radio_state_t *state, internet_radio_event_t event);
bool internet_radio_state_output_enabled(internet_radio_state_t state);
bool internet_radio_output_start_once(bool *output_started);
