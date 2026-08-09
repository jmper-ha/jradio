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
    return state == INTERNET_RADIO_STATE_PLAYING;
}

bool internet_radio_output_start_once(bool *output_started)
{
    if (output_started == NULL || *output_started) {
        return false;
    }
    *output_started = true;
    return true;
}

bool internet_radio_output_needs_restart(uint32_t current_sample_rate,
                                         uint32_t next_sample_rate,
                                         bool output_started)
{
    return output_started && current_sample_rate > 0U && next_sample_rate > 0U &&
           current_sample_rate != next_sample_rate;
}

internet_radio_read_action_t internet_radio_read_classify(int result, int try_again_result)
{
    if (result > 0) {
        return INTERNET_RADIO_READ_DATA;
    }
    if (result == try_again_result) {
        return INTERNET_RADIO_READ_RETRY;
    }
    return result == 0 ? INTERNET_RADIO_READ_CLOSED : INTERNET_RADIO_READ_ERROR;
}

bool internet_radio_input_buffer_stalled(bool need_input, size_t available, size_t capacity)
{
    return need_input && capacity > 0U && available >= capacity;
}
