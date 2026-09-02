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
    assert(!internet_radio_state_output_enabled(INTERNET_RADIO_STATE_CONNECTING));
    assert(internet_radio_state_output_enabled(INTERNET_RADIO_STATE_PLAYING));
    assert(!internet_radio_state_output_enabled(INTERNET_RADIO_STATE_PAUSED));
    assert(!internet_radio_state_output_enabled(INTERNET_RADIO_STATE_RECONNECTING));
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

static void test_running_output_restarts_when_decoder_sample_rate_changes(void)
{
    assert(internet_radio_output_needs_restart(22050U, 44100U, true));
    assert(!internet_radio_output_needs_restart(44100U, 44100U, true));
    assert(!internet_radio_output_needs_restart(22050U, 44100U, false));
    assert(!internet_radio_output_needs_restart(0U, 44100U, true));
}

static void test_http_read_results_are_classified(void)
{
    const int try_again = -1234;

    assert(internet_radio_read_classify(128, try_again) == INTERNET_RADIO_READ_DATA);
    assert(internet_radio_read_classify(try_again, try_again) == INTERNET_RADIO_READ_RETRY);
    assert(internet_radio_read_classify(0, try_again) == INTERNET_RADIO_READ_CLOSED);
    assert(internet_radio_read_classify(-1, try_again) == INTERNET_RADIO_READ_ERROR);
}

static void test_full_input_buffer_is_fatal_only_when_decoder_needs_more_data(void)
{
    assert(internet_radio_input_buffer_stalled(true, 16384U, 16384U));
    assert(!internet_radio_input_buffer_stalled(false, 16384U, 16384U));
    assert(!internet_radio_input_buffer_stalled(true, 8192U, 16384U));
}

static void test_a_decoder_that_eats_input_without_producing_is_a_stall(void)
{
    /* The failure this exists for: a station whose intro is spliced onto the
     * live stream leaves the MP3 bit reservoir broken across the join, so every
     * following frame is rejected. The decoder keeps consuming a frame per call
     * and emitting nothing, which reads as healthy from the buffer's point of
     * view and sounds like permanent silence. */
    assert(internet_radio_decode_stalled(2000U, 65536U, 2000U));
    assert(internet_radio_decode_stalled(5000U, 208U, 2000U));

    /* Below the limit is just a decoder between frames. */
    assert(!internet_radio_decode_stalled(1999U, 65536U, 2000U));

    /* Nothing to decode is ordinary starvation, counted elsewhere; calling it a
     * decoder stall would blame the decoder for a slow network. */
    assert(!internet_radio_decode_stalled(60000U, 0U, 2000U));

    /* A zero limit turns the check off rather than firing on every pass. */
    assert(!internet_radio_decode_stalled(60000U, 65536U, 0U));
}

static void test_the_stall_limit_leaves_room_for_a_decoder_still_finding_a_frame(void)
{
    /* The two measured regimes, 2026-09-02, eleven stations across MP3, AAC and
     * FLAC: 41 ms was the longest gap during playback, while finding the first
     * frame of a fresh stream took up to 430 ms. */
    assert(internet_radio_decode_stall_limit(true, 400U, 2000U) == 400U);
    assert(internet_radio_decode_stall_limit(false, 400U, 2000U) == 2000U);

    /* Which is the whole point: at 430 ms a decoder that has yet to produce is
     * doing its job, and resetting it there would fire on every station change.
     * The same 430 ms from a decoder that was playing is the fault this
     * watchdog exists for. */
    assert(!internet_radio_decode_stalled(
        430U, 65536U, internet_radio_decode_stall_limit(false, 400U, 2000U)));
    assert(internet_radio_decode_stalled(
        430U, 65536U, internet_radio_decode_stall_limit(true, 400U, 2000U)));

    /* A reset sends the decoder back to hunting, so the long limit covers the
     * second wait too - otherwise a reset that was about to work would be cut
     * short into a reconnect. */
    assert(!internet_radio_decode_stalled(
        1999U, 65536U, internet_radio_decode_stall_limit(false, 400U, 2000U)));

    /* Nothing to decode is still ordinary starvation under either limit. */
    assert(!internet_radio_decode_stalled(
        60000U, 0U, internet_radio_decode_stall_limit(true, 400U, 2000U)));
}

int main(void)
{
    test_start_pause_resume_stop();
    test_error_and_reconnect_paths();
    test_audio_output_gate_follows_state();
    test_first_pcm_starts_output_once();
    test_running_output_restarts_when_decoder_sample_rate_changes();
    test_http_read_results_are_classified();
    test_full_input_buffer_is_fatal_only_when_decoder_needs_more_data();
    test_a_decoder_that_eats_input_without_producing_is_a_stall();
    test_the_stall_limit_leaves_room_for_a_decoder_still_finding_a_frame();
    puts("internet_radio_state tests passed");
    return 0;
}
