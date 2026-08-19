#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

typedef enum {
    INTERNET_RADIO_READ_DATA = 0,
    INTERNET_RADIO_READ_RETRY,
    INTERNET_RADIO_READ_CLOSED,
    INTERNET_RADIO_READ_ERROR,
} internet_radio_read_action_t;

bool internet_radio_state_apply(internet_radio_state_t *state, internet_radio_event_t event);
bool internet_radio_state_output_enabled(internet_radio_state_t state);
bool internet_radio_output_start_once(bool *output_started);
bool internet_radio_output_needs_restart(uint32_t current_sample_rate,
                                         uint32_t next_sample_rate,
                                         bool output_started);
internet_radio_read_action_t internet_radio_read_classify(int result, int try_again_result);
bool internet_radio_input_buffer_stalled(bool need_input, size_t available, size_t capacity);

/* True when the decoder has had data to work with and produced no samples for
 * `limit_ms`. Separate from the buffer-full check above: a decoder can consume
 * input steadily and still emit nothing, which looks like healthy progress
 * from every other angle and sounds like silence. */
bool internet_radio_decode_stalled(uint32_t elapsed_ms, size_t available, uint32_t limit_ms);
