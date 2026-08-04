#include "internet_radio_state.h"

#include <stddef.h>

bool internet_radio_state_apply(internet_radio_state_t *state, internet_radio_event_t event)
{
    if (state == NULL) {
        return false;
    }

    switch (event) {
    case INTERNET_RADIO_EVENT_START:
        if (*state != INTERNET_RADIO_STATE_STOPPED && *state != INTERNET_RADIO_STATE_ERROR) return false;
        *state = INTERNET_RADIO_STATE_CONNECTING;
        return true;
    case INTERNET_RADIO_EVENT_PLAYER_RUNNING:
        if (*state != INTERNET_RADIO_STATE_CONNECTING && *state != INTERNET_RADIO_STATE_RECONNECTING) return false;
        *state = INTERNET_RADIO_STATE_PLAYING;
        return true;
    case INTERNET_RADIO_EVENT_PAUSE:
        if (*state != INTERNET_RADIO_STATE_PLAYING) return false;
        *state = INTERNET_RADIO_STATE_PAUSED;
        return true;
    case INTERNET_RADIO_EVENT_RESUME:
        if (*state != INTERNET_RADIO_STATE_PAUSED) return false;
        *state = INTERNET_RADIO_STATE_PLAYING;
        return true;
    case INTERNET_RADIO_EVENT_STOP:
        *state = INTERNET_RADIO_STATE_STOPPED;
        return true;
    case INTERNET_RADIO_EVENT_FAILURE:
        if (*state == INTERNET_RADIO_STATE_STOPPED || *state == INTERNET_RADIO_STATE_ERROR) return false;
        *state = INTERNET_RADIO_STATE_RECONNECTING;
        return true;
    case INTERNET_RADIO_EVENT_RETRY:
        if (*state != INTERNET_RADIO_STATE_RECONNECTING) return false;
        *state = INTERNET_RADIO_STATE_CONNECTING;
        return true;
    case INTERNET_RADIO_EVENT_FATAL:
        *state = INTERNET_RADIO_STATE_ERROR;
        return true;
    }
    return false;
}

bool internet_radio_state_output_enabled(internet_radio_state_t state)
{
    return state == INTERNET_RADIO_STATE_CONNECTING ||
           state == INTERNET_RADIO_STATE_PLAYING ||
           state == INTERNET_RADIO_STATE_RECONNECTING;
}

bool internet_radio_output_start_once(bool *output_started)
{
    if (output_started == NULL || *output_started) {
        return false;
    }
    *output_started = true;
    return true;
}
