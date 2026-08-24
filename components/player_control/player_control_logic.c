#include "player_control.h"

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
           memcmp(left->error, right->error, sizeof(left->error)) == 0;
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
                                                                      : 0U;
        const bool supported = needed != 0U && (state->capabilities & needed) != 0U;
        return supported ? PLAYER_OPERATION_SELECT_SOURCE : PLAYER_OPERATION_INVALID;
    }
    case PLAYER_COMMAND_STOP_SOURCE:
        return state->active_source == AUDIO_SOURCE_NONE ? PLAYER_OPERATION_NONE
                                                          : PLAYER_OPERATION_STOP;
    case PLAYER_COMMAND_PLAY:
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
        if (state->playback_state == PLAYER_PLAYBACK_PAUSED) return PLAYER_OPERATION_RESUME;
        if (state->playback_state == PLAYER_PLAYBACK_STOPPED ||
            state->playback_state == PLAYER_PLAYBACK_ERROR) return PLAYER_OPERATION_START_SAVED;
        return state->playback_state == PLAYER_PLAYBACK_CONNECTING ||
                       state->playback_state == PLAYER_PLAYBACK_RECONNECTING
                   ? PLAYER_OPERATION_NONE : PLAYER_OPERATION_INVALID;
    case PLAYER_COMMAND_SELECT_ITEM: {
        if ((!audio_source_is_stations(state->active_source) &&
             !audio_source_is_files(state->active_source)) ||
            command->item_index >= state->item_count) return PLAYER_OPERATION_INVALID;
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
    case PLAYER_COMMAND_BROWSE_UP:
        // Whether there is anywhere to go depends on the path, which this pure
        // function cannot see; the executor refuses at the mount root.
        return audio_source_is_files(state->active_source) ? PLAYER_OPERATION_BROWSE_UP
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
        if (state->active_source != AUDIO_SOURCE_YANDEX) return PLAYER_OPERATION_INVALID;
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
        if (state->active_item_index == PLAYER_ITEM_NONE) return PLAYER_OPERATION_NONE;
        if (audio_source_is_files(state->active_source)) {
            /* Which row holds the neighbouring *file* cannot be worked out
             * here: a directory listing holds directories too, and this
             * function cannot see them. The ends of the directory are refused
             * by the executor, which can. */
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
        /* Only a track that belongs to an account can carry a mark, and only
         * while one is on the air: the mark names the track, not the station
         * that handed it out. Paused counts - the track is still the one being
         * listened to. */
        if (state->active_source != AUDIO_SOURCE_YANDEX) return PLAYER_OPERATION_INVALID;
        return state->playback_state == PLAYER_PLAYBACK_PLAYING ||
                       state->playback_state == PLAYER_PLAYBACK_PAUSED
                   ? PLAYER_OPERATION_TOGGLE_LIKE
                   : PLAYER_OPERATION_INVALID;
    case PLAYER_COMMAND_TRACK_FINISHED:
        // Only USB has tracks that end. A radio stream that stops has failed
        // and must not silently jump to another station.
        return audio_source_is_files(state->active_source) ? PLAYER_OPERATION_ADVANCE_ITEM
                                                        : PLAYER_OPERATION_NONE;
    default:
        return PLAYER_OPERATION_INVALID;
    }
}
