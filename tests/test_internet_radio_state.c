#include <assert.h>
#include <stdio.h>

#include "internet_radio_state.h"

static void test_start_pause_resume_stop(void)
{
    internet_radio_state_t state = INTERNET_RADIO_STATE_STOPPED;

    assert(internet_radio_state_apply(&state, INTERNET_RADIO_EVENT_START));
    assert(state == INTERNET_RADIO_STATE_CONNECTING);
    assert(internet_radio_state_apply(&state, INTERNET_RADIO_EVENT_PLAYER_RUNNING));
    assert(state == INTERNET_RADIO_STATE_PLAYING);
    assert(internet_radio_state_apply(&state, INTERNET_RADIO_EVENT_PAUSE));
    assert(state == INTERNET_RADIO_STATE_PAUSED);
    assert(internet_radio_state_apply(&state, INTERNET_RADIO_EVENT_RESUME));
    assert(state == INTERNET_RADIO_STATE_PLAYING);
    assert(internet_radio_state_apply(&state, INTERNET_RADIO_EVENT_STOP));
    assert(state == INTERNET_RADIO_STATE_STOPPED);
}

static void test_error_and_reconnect_paths(void)
{
    internet_radio_state_t state = INTERNET_RADIO_STATE_CONNECTING;

    assert(internet_radio_state_apply(&state, INTERNET_RADIO_EVENT_FAILURE));
    assert(state == INTERNET_RADIO_STATE_RECONNECTING);
    assert(internet_radio_state_apply(&state, INTERNET_RADIO_EVENT_RETRY));
    assert(state == INTERNET_RADIO_STATE_CONNECTING);
    assert(internet_radio_state_apply(&state, INTERNET_RADIO_EVENT_FATAL));
    assert(state == INTERNET_RADIO_STATE_ERROR);
    assert(!internet_radio_state_apply(&state, INTERNET_RADIO_EVENT_RESUME));
}

static void test_audio_output_gate_follows_state(void)
{
    assert(!internet_radio_state_output_enabled(INTERNET_RADIO_STATE_STOPPED));
    assert(internet_radio_state_output_enabled(INTERNET_RADIO_STATE_CONNECTING));
    assert(internet_radio_state_output_enabled(INTERNET_RADIO_STATE_PLAYING));
    assert(!internet_radio_state_output_enabled(INTERNET_RADIO_STATE_PAUSED));
    assert(internet_radio_state_output_enabled(INTERNET_RADIO_STATE_RECONNECTING));
    assert(!internet_radio_state_output_enabled(INTERNET_RADIO_STATE_ERROR));
}

static void test_first_pcm_starts_output_once(void)
{
    bool output_started = false;

    assert(internet_radio_output_start_once(&output_started));
    assert(output_started);
    assert(!internet_radio_output_start_once(&output_started));
    assert(!internet_radio_output_start_once(NULL));
}

int main(void)
{
    test_start_pause_resume_stop();
    test_error_and_reconnect_paths();
    test_audio_output_gate_follows_state();
    test_first_pcm_starts_output_once();
    puts("internet_radio_state tests passed");
    return 0;
}
