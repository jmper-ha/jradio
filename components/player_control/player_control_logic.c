#include "player_control.h"

#include <stdio.h>
#include <string.h>

#include "file_player_state.h"

#define PLAYER_RSSI_REFRESH_MS 1000U

bool player_rssi_refresh_due(bool seen, uint32_t last_update_ms,
                             uint32_t now_ms)
{
    return !seen ||
           (uint32_t)(now_ms - last_update_ms) >= PLAYER_RSSI_REFRESH_MS;
}

player_playback_state_t player_playback_from_radio(internet_radio_state_t state)
{
    switch (state) {
    case INTERNET_RADIO_STATE_STOPPED:
        return PLAYER_PLAYBACK_STOPPED;
    case INTERNET_RADIO_STATE_CONNECTING:
        return PLAYER_PLAYBACK_CONNECTING;
    case INTERNET_RADIO_STATE_PLAYING:
        return PLAYER_PLAYBACK_PLAYING;
    case INTERNET_RADIO_STATE_PAUSED:
        return PLAYER_PLAYBACK_PAUSED;
    case INTERNET_RADIO_STATE_RECONNECTING:
        return PLAYER_PLAYBACK_RECONNECTING;
    case INTERNET_RADIO_STATE_ERROR:
        return PLAYER_PLAYBACK_ERROR;
    default:
        return PLAYER_PLAYBACK_ERROR;
    }
}

player_playback_state_t player_playback_from_file(file_player_state_t state)
{
    switch (state) {
    case FILE_PLAYER_STATE_STOPPED:
        return PLAYER_PLAYBACK_STOPPED;
    // A local file has nothing to connect to, but the decoder still has to
    // find its first frame, so the UI gets the same "working on it" state it
    // already knows how to render.
    case FILE_PLAYER_STATE_STARTING:
        return PLAYER_PLAYBACK_CONNECTING;
    case FILE_PLAYER_STATE_PLAYING:
        return PLAYER_PLAYBACK_PLAYING;
    case FILE_PLAYER_STATE_PAUSED:
        return PLAYER_PLAYBACK_PAUSED;
    case FILE_PLAYER_STATE_ERROR:
    default:
        return PLAYER_PLAYBACK_ERROR;
    }
}

bool player_file_same_file_playing(const char *path, const char *playing_path,
                                  player_playback_state_t state)
{
    if (path == NULL || playing_path == NULL || playing_path[0] == '\0') return false;
    /* Only while the file is actually up. A track that failed has to stay
     * restartable - the path is still remembered after the decoder gave up,
     * and refusing to start it again would leave the user pressing a row that
     * does nothing. */
    if (state != PLAYER_PLAYBACK_PLAYING && state != PLAYER_PLAYBACK_PAUSED &&
        state != PLAYER_PLAYBACK_CONNECTING) {
        return false;
    }
    return strcmp(path, playing_path) == 0;
}

bool player_snapshot_equal(const player_snapshot_t *left,
                           const player_snapshot_t *right)
{
    if (left == right) {
        return true;
    }
    if (left == NULL || right == NULL) {
        return false;
    }
    return left->capabilities == right->capabilities &&
           left->active_source == right->active_source &&
           left->playback_state == right->playback_state &&
           left->active_item_index == right->active_item_index &&
           left->item_count == right->item_count &&
           left->browse_has_parent == right->browse_has_parent &&
           memcmp(left->context, right->context, sizeof(left->context)) == 0 &&
           memcmp(left->stream_title, right->stream_title,
                  sizeof(left->stream_title)) == 0 &&
           memcmp(left->codec, right->codec, sizeof(left->codec)) == 0 &&
           left->bitrate_kbps == right->bitrate_kbps &&
           left->sample_rate_hz == right->sample_rate_hz &&
           left->wifi_rssi_valid == right->wifi_rssi_valid &&
           left->wifi_rssi_dbm == right->wifi_rssi_dbm &&
           left->track_likeable == right->track_likeable &&
           left->track_liked == right->track_liked &&
           left->track_disliked == right->track_disliked &&
           left->fm_khz == right->fm_khz &&
           left->fm_stereo == right->fm_stereo && left->fm_signal == right->fm_signal &&
           left->fm_rds == right->fm_rds &&
           memcmp(left->error, right->error, sizeof(left->error)) == 0;
}

/* Nothing that streams from the internet can be started without one, and the
 * only thing further down the road is a connection attempt that ends in
 * "Connection error" on the screen. Both faces refuse it before it is offered,
 * but the refusal belongs here as well: a page loaded before the network went
 * away still has live buttons, and a browser that kept it open is exactly how
 * this was found. */
static bool player_source_needs_absent_network(const player_snapshot_t *state,
                                               audio_source_t source)
{
    return !state->wifi_connected && (source == AUDIO_SOURCE_INTERNET_RADIO ||
                                      source == AUDIO_SOURCE_YANDEX ||
                                      /* The server is on the LAN rather than
                                       * on the internet, but it is still the
                                       * network: with no join there is nothing
                                       * to search and nothing to stream. */
                                      source == AUDIO_SOURCE_DLNA);
}

player_operation_t player_control_decide(const player_snapshot_t *state,
                                         const player_command_t *command)
{
    if (state == NULL || command == NULL) return PLAYER_OPERATION_INVALID;
    switch (command->kind) {
    case PLAYER_COMMAND_SELECT_SOURCE: {
        // Unlike a plain no-op re-select, a source stuck in ERROR must stay
        // retryable: re-selecting it should restart it, not be swallowed.
        const bool already_healthy = command->source == state->active_source &&
            state->playback_state != PLAYER_PLAYBACK_ERROR;
        if (already_healthy) return PLAYER_OPERATION_NONE;
        /* Each volume answers for itself: a mounted drive says nothing about
         * what is in the card slot, and one capability for both would have let
         * the card be selected whenever a stick happened to be plugged in. */
        const uint32_t needed = command->source == AUDIO_SOURCE_INTERNET_RADIO
                                    ? PLAYER_CAP_INTERNET_RADIO
                                : command->source == AUDIO_SOURCE_USB ? PLAYER_CAP_USB
                                : command->source == AUDIO_SOURCE_SD  ? PLAYER_CAP_SD
                                : command->source == AUDIO_SOURCE_YANDEX ? PLAYER_CAP_YANDEX
                                : command->source == AUDIO_SOURCE_DLNA ? PLAYER_CAP_DLNA
                                : command->source == AUDIO_SOURCE_BLUETOOTH ? PLAYER_CAP_BLUETOOTH
                                : command->source == AUDIO_SOURCE_FM ? PLAYER_CAP_FM
                                                                            : 0U;
        const bool supported = needed != 0U && (state->capabilities & needed) != 0U &&
                               !player_source_needs_absent_network(state, command->source);
        return supported ? PLAYER_OPERATION_SELECT_SOURCE : PLAYER_OPERATION_INVALID;
    }
    case PLAYER_COMMAND_STOP_SOURCE:
        return state->active_source == AUDIO_SOURCE_NONE ? PLAYER_OPERATION_NONE
                                                          : PLAYER_OPERATION_STOP;
    case PLAYER_COMMAND_PLAY:
        // Whatever is already playing may keep playing; nothing starts.
        if (state->playback_state != PLAYER_PLAYBACK_PLAYING &&
            player_source_needs_absent_network(state, state->active_source)) {
            return PLAYER_OPERATION_INVALID;
        }
        if (state->playback_state == PLAYER_PLAYBACK_PAUSED) return PLAYER_OPERATION_RESUME;
        if (state->playback_state == PLAYER_PLAYBACK_STOPPED ||
            state->playback_state == PLAYER_PLAYBACK_ERROR) return PLAYER_OPERATION_START_SAVED;
        return state->playback_state == PLAYER_PLAYBACK_PLAYING ||
                       state->playback_state == PLAYER_PLAYBACK_CONNECTING ||
                       state->playback_state == PLAYER_PLAYBACK_RECONNECTING
                   ? PLAYER_OPERATION_NONE : PLAYER_OPERATION_INVALID;
    case PLAYER_COMMAND_PAUSE:
        if (state->playback_state == PLAYER_PLAYBACK_PLAYING) return PLAYER_OPERATION_PAUSE;
        return state->playback_state == PLAYER_PLAYBACK_PAUSED ? PLAYER_OPERATION_NONE
                                                                : PLAYER_OPERATION_INVALID;
    case PLAYER_COMMAND_TOGGLE:
        if (state->playback_state == PLAYER_PLAYBACK_PLAYING) return PLAYER_OPERATION_PAUSE;
        if (player_source_needs_absent_network(state, state->active_source)) {
            return PLAYER_OPERATION_INVALID;
        }
        if (state->playback_state == PLAYER_PLAYBACK_PAUSED) return PLAYER_OPERATION_RESUME;
        if (state->playback_state == PLAYER_PLAYBACK_STOPPED ||
            state->playback_state == PLAYER_PLAYBACK_ERROR) return PLAYER_OPERATION_START_SAVED;
        return state->playback_state == PLAYER_PLAYBACK_CONNECTING ||
                       state->playback_state == PLAYER_PLAYBACK_RECONNECTING
                   ? PLAYER_OPERATION_NONE : PLAYER_OPERATION_INVALID;
    case PLAYER_COMMAND_SELECT_ITEM: {
        /* With no source selected the snapshot still describes the station
         * catalogue - item_count comes from it, and both screens show the
         * list. This used to be dropped in silence: after a reboot neither a
         * press of the encoder nor a click on a row started anything until the
         * source was selected separately. The executor marks the radio as the
         * active source when it accepts such a command. */
        const bool stations = audio_source_is_stations(state->active_source) ||
                              state->active_source == AUDIO_SOURCE_NONE;
        /* The lists a row can mean two things in: a volume's directory and a
         * media server's container both hold things to open beside things to
         * play. */
        const bool browsable = audio_source_is_files(state->active_source) ||
                               state->active_source == AUDIO_SOURCE_DLNA;
        /* A preset needs no network and has no "same row still healthy" to
         * spare: choosing the one on the air while it is muted unmutes it. */
        if (state->active_source == AUDIO_SOURCE_FM) {
            return command->item_index < state->item_count ? PLAYER_OPERATION_START_ITEM
                                                           : PLAYER_OPERATION_INVALID;
        }
        if ((!stations && !browsable) || command->item_index >= state->item_count) {
            return PLAYER_OPERATION_INVALID;
        }
        /* Every station list is a list of streams, including the one the
         * snapshot describes before any source is chosen - and a media server's
         * rows are streams too, only from the LAN. */
        if ((stations || state->active_source == AUDIO_SOURCE_DLNA) &&
            !state->wifi_connected) {
            return PLAYER_OPERATION_INVALID;
        }
        // On USB an entry can be a directory, and re-selecting the directory
        // the cursor is already in still has to navigate; only a station list
        // can treat "same index, still healthy" as a no-op.
        const bool already_healthy = audio_source_is_stations(state->active_source) &&
            command->item_index == state->active_item_index &&
            (state->playback_state == PLAYER_PLAYBACK_CONNECTING ||
             state->playback_state == PLAYER_PLAYBACK_PLAYING ||
             state->playback_state == PLAYER_PLAYBACK_PAUSED ||
             state->playback_state == PLAYER_PLAYBACK_RECONNECTING);
        return already_healthy ? PLAYER_OPERATION_NONE : PLAYER_OPERATION_START_ITEM;
    }
    case PLAYER_COMMAND_TEST_STREAM:
        /* Whatever is playing gives way: the button that sends this says it
         * will try the station now, and one output cannot do both. The URL is
         * not in the command, so there is nothing here to check it against -
         * the executor refuses an empty one. */
        return PLAYER_OPERATION_TEST_STREAM;
    case PLAYER_COMMAND_BROWSE_UP:
        /* Whether there is anywhere to go depends on where the browser is,
         * which this pure function cannot see; the executor refuses at the
         * root. A media server is a tree like a volume is, and going up in one
         * is the same gesture - the difference is only that a server's trail
         * is remembered rather than cut off a path. */
        return audio_source_is_files(state->active_source) ||
                       state->active_source == AUDIO_SOURCE_DLNA
                   ? PLAYER_OPERATION_BROWSE_UP
                   : PLAYER_OPERATION_INVALID;
    case PLAYER_COMMAND_BROWSE_REVEAL:
        if (!audio_source_is_files(state->active_source)) return PLAYER_OPERATION_INVALID;
        // With nothing playing there is nothing to reveal, and the file the
        // drive played last is not it: after a stop, or a drive pulled and put
        // back, the browser belongs wherever it already is.
        return state->playback_state == PLAYER_PLAYBACK_STOPPED ||
                       state->playback_state == PLAYER_PLAYBACK_ERROR
                   ? PLAYER_OPERATION_NONE
                   : PLAYER_OPERATION_BROWSE_REVEAL;
    case PLAYER_COMMAND_SEEK:
        // A stream has no length and no position to move to, and a track that
        // is stopped or failed has no decoder to move. Paused counts because
        // the file is still open at a position: the jump is held until
        // playback resumes.
        if (!audio_source_is_files(state->active_source)) return PLAYER_OPERATION_INVALID;
        return state->playback_state == PLAYER_PLAYBACK_PLAYING ||
                       state->playback_state == PLAYER_PLAYBACK_PAUSED
                   ? PLAYER_OPERATION_SEEK
                   : PLAYER_OPERATION_INVALID;
    case PLAYER_COMMAND_NEXT_TRACK:
        /* Only the rotor hands out tracks one after another. Asking a station
         * for the next track is meaningless, and a file list has no forward
         * button by design - it advances when a track ends. */
        /* And the phone: its queue is the rotor's shape, tracks one after
         * another with a skip key. */
        if (state->active_source != AUDIO_SOURCE_YANDEX &&
            state->active_source != AUDIO_SOURCE_BLUETOOTH) {
            return PLAYER_OPERATION_INVALID;
        }
        /* Paused counts: skipping while paused lines the next track up, and
         * the alternative is a button that does nothing until you resume. */
        return state->playback_state == PLAYER_PLAYBACK_PLAYING ||
                       state->playback_state == PLAYER_PLAYBACK_PAUSED
                   ? PLAYER_OPERATION_NEXT_TRACK
                   : PLAYER_OPERATION_INVALID;
    case PLAYER_COMMAND_PREVIOUS_ITEM:
    case PLAYER_COMMAND_NEXT_ITEM: {
        const bool forward = command->kind == PLAYER_COMMAND_NEXT_ITEM;
        const player_operation_t step =
            forward ? PLAYER_OPERATION_NEXT_ITEM : PLAYER_OPERATION_PREVIOUS_ITEM;
        /* The two track keys move what is playing along the list it came from.
         * With nothing playing there is no place in the list to move from, and
         * starting something would be a different gesture than the one asked
         * for. */
        /* The phone has no list here at all - the two keys go to it as its
         * own previous/next, whenever a track is on. */
        /* The tuner has no list yet either: the keys seek to the next
         * station up or down the band, which is the chip's own search. */
        if (state->active_source == AUDIO_SOURCE_BLUETOOTH ||
            state->active_source == AUDIO_SOURCE_FM) {
            return state->playback_state == PLAYER_PLAYBACK_PLAYING ||
                           state->playback_state == PLAYER_PLAYBACK_PAUSED
                       ? step
                       : PLAYER_OPERATION_NONE;
        }
        if (state->active_item_index == PLAYER_ITEM_NONE) return PLAYER_OPERATION_NONE;
        if (audio_source_is_files(state->active_source) ||
            state->active_source == AUDIO_SOURCE_DLNA) {
            /* Which row holds the neighbouring *track* cannot be worked out
             * here: a directory listing holds directories too, and a media
             * server's listing holds containers and rows this build cannot
             * decode. This function sees none of that. The ends are refused by
             * the executor, which can. */
            return step;
        }
        /* Not the rotor, even though it is a station source: its chain has no
         * previous track and no index in any list, and its keys mean something
         * else entirely - the next track, and the like mark. */
        if (state->active_source != AUDIO_SOURCE_INTERNET_RADIO) {
            return PLAYER_OPERATION_INVALID;
        }
        if (state->active_item_index >= state->item_count) return PLAYER_OPERATION_NONE;
        /* Nothing wraps: at either end of the catalog the key does nothing.
         * A list that rolls over from the last station to the first hides the
         * fact that the end was reached. */
        if (forward) {
            return state->active_item_index + 1U < state->item_count ? step
                                                                     : PLAYER_OPERATION_NONE;
        }
        return state->active_item_index > 0U ? step : PLAYER_OPERATION_NONE;
    }
    case PLAYER_COMMAND_TOGGLE_LIKE:
    case PLAYER_COMMAND_TOGGLE_DISLIKE:
        /* Only a track that belongs to an account can carry a mark, and only
         * while one is on the air: the mark names the track, not the station
         * that handed it out. Paused counts - the track is still the one being
         * listened to. Both marks answer to the same conditions, so they are
         * decided together and only the operation differs. */
        if (state->active_source != AUDIO_SOURCE_YANDEX) return PLAYER_OPERATION_INVALID;
        if (state->playback_state != PLAYER_PLAYBACK_PLAYING &&
            state->playback_state != PLAYER_PLAYBACK_PAUSED) {
            return PLAYER_OPERATION_INVALID;
        }
        return command->kind == PLAYER_COMMAND_TOGGLE_DISLIKE
                   ? PLAYER_OPERATION_TOGGLE_DISLIKE
                   : PLAYER_OPERATION_TOGGLE_LIKE;
    case PLAYER_COMMAND_TRACK_FINISHED:
        // Only USB has tracks that end. A radio stream that stops has failed
        // and must not silently jump to another station.
        return audio_source_is_files(state->active_source) ? PLAYER_OPERATION_ADVANCE_ITEM
                                                        : PLAYER_OPERATION_NONE;
    case PLAYER_COMMAND_FM_TUNE:
        /* Paused counts: the tuner unmutes on the station it is sent to, as
         * it does for a preset. The band is the chip's. */
        if (state->active_source != AUDIO_SOURCE_FM || command->frequency_khz < 87000U ||
            command->frequency_khz > 108000U) {
            return PLAYER_OPERATION_INVALID;
        }
        return PLAYER_OPERATION_FM_TUNE;
    case PLAYER_COMMAND_FM_SEEK_UP:
    case PLAYER_COMMAND_FM_SEEK_DOWN:
        if (state->active_source != AUDIO_SOURCE_FM) return PLAYER_OPERATION_INVALID;
        return command->kind == PLAYER_COMMAND_FM_SEEK_UP ? PLAYER_OPERATION_FM_SEEK_UP
                                                           : PLAYER_OPERATION_FM_SEEK_DOWN;
    case PLAYER_COMMAND_CUE_TRACK:
        // News from the file player about a file it is still playing; the
        // source may have moved on since it was posted, and then it is stale.
        return audio_source_is_files(state->active_source) ? PLAYER_OPERATION_CUE_TRACK
                                                        : PLAYER_OPERATION_NONE;
    default:
        return PLAYER_OPERATION_INVALID;
    }
}

bool player_media_removal_clears_cover(audio_source_t active_source)
{
    /* The card is not hot-removable on this board - there is no detect line,
     * and it is unmounted when its source is stopped - so this is only ever
     * asked about the drive. It still answers for the volume sources together:
     * the question is whether the picture came off a volume at all. */
    return audio_source_is_files(active_source);
}

void player_fm_frequency_text(uint32_t khz, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0U) return;
    const uint32_t tenths = (khz + 50U) / 100U;
    snprintf(out, out_size, "%u.%u", (unsigned)(tenths / 10U), (unsigned)(tenths % 10U));
}

/* Where each step starts. A short wire picks the local stations up at 40-60,
 * and below 20 there is mostly hiss. */
static const uint8_t k_fm_bar_floor[PLAYER_FM_SIGNAL_BARS + 1] = {0U, 18U, 28U, 38U, 48U, 58U};
#define PLAYER_FM_SIGNAL_MARGIN 3U

int player_fm_signal_bars(uint8_t rssi, int previous)
{
    int bars = 0;
    for (int step = PLAYER_FM_SIGNAL_BARS; step > 0; --step) {
        if (rssi >= k_fm_bar_floor[step]) {
            bars = step;
            break;
        }
    }
    if (previous < 0 || previous > PLAYER_FM_SIGNAL_BARS || bars == previous) return bars;
    // Stay put unless the reading is clear of the step being left.
    if (bars > previous && rssi < k_fm_bar_floor[previous + 1] + PLAYER_FM_SIGNAL_MARGIN) {
        return previous;
    }
    if (bars < previous && rssi + PLAYER_FM_SIGNAL_MARGIN > k_fm_bar_floor[previous]) {
        return previous;
    }
    return bars;
}

bool player_fm_name_is_frequency(const char *name, uint32_t khz)
{
    if (name == NULL) return false;
    char digits[12];
    size_t count = 0U;
    for (const char *at = name; *at != '\0'; ++at) {
        if (*at < '0' || *at > '9') continue;
        if (count + 1U >= sizeof(digits)) return false;
        digits[count++] = *at;
    }
    digits[count] = '\0';
    if (count == 0U) return false;
    char tenths[12];
    snprintf(tenths, sizeof(tenths), "%u", (unsigned)((khz + 50U) / 100U));
    return strcmp(digits, tenths) == 0;
}

player_fm_scan_step_t player_fm_scan_merge(uint32_t last_khz, uint8_t kept_rssi, uint32_t khz,
                                           uint8_t rssi)
{
    if (last_khz == 0U || khz != last_khz + 100U) return PLAYER_FM_SCAN_NEW;
    return rssi > kept_rssi ? PLAYER_FM_SCAN_REPLACE : PLAYER_FM_SCAN_DROP;
}
