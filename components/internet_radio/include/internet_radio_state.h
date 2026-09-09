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

/* Whether a decoder that asked for more data is actually starved, or merely
 * walking through the backlog it already has.
 *
 * "Need more data" covers two different situations and they want opposite
 * answers. A decoder that could make no use at all of what it was given
 * (`consumed` 0) is starved and has to wait for the network. A decoder that
 * consumed bytes and produced no samples is resyncing: an MP3 stream joined
 * mid-frame has no frame header at its first byte, and the decoder walks
 * forward a byte at a time until it finds one.
 *
 * Treating the second as starvation is what made a station that opens
 * mid-frame take twenty seconds to start. Measured on Swing Street Radio,
 * whose Icecast mount bursts from wherever its buffer happens to be: the first
 * frame header sits 61-377 bytes in, varying per connection, and each byte of
 * resync cost a whole network round trip. About eighty bytes fit in the two
 * seconds before the stall check gave up, so most connections were dropped and
 * retried until one happened to open close enough to a frame boundary.
 *
 * `remaining` is the backlog left after the call. With nothing left there is
 * nothing to walk through, so that is starvation too. */
bool internet_radio_decoder_starved(size_t consumed, size_t remaining);

/* Which limit the check above runs with. A decoder that was producing and then
 * stopped is broken within milliseconds; one that has not produced yet is still
 * hunting for its first frame, which is slower and entirely normal - measured at
 * up to 430 ms on a fresh stream against 41 ms for the worst gap seen during
 * playback. That gap between the two is why this is a choice rather than one
 * compromise value, and why the short limit must never reach the hunting case:
 * there it would fire on every station change and turn an ordinary start into a
 * reset and then a reconnect. */
uint32_t internet_radio_decode_stall_limit(bool decoder_synced, uint32_t running_limit_ms,
                                           uint32_t sync_limit_ms);
