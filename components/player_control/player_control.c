#include "player_control.h"

#include "radio_prebuffer.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "album_art.h"
#include "audio_source.h"
#include "board_features.h"
#include "internet_radio.h"
#include "yandex_auth.h"
#include "yandex_catalog.h"
#include "yandex_likes.h"
#include "yandex_rotor.h"
#include "file_browser.h"
#include "file_storage.h"
#include "file_player.h"
#include "sd_storage.h"
#include "usb_storage.h"
#include "wifi_provisioning.h"

#define PLAYER_COMMAND_QUEUE_LENGTH 8U
/* Measured, not guessed. The deep path is not playback but browsing: opening a
 * directory runs on this task and descends through FATFS into the MSC
 * transfers, and it left 1368 bytes of 6144. That is the thinnest margin in the
 * system, and this is the task that overflowed the one time the build was
 * compiled at -O2, where inlining costs more stack again. */
/* 10 KB rather than 8. Starting a Yandex station resolves its first track on
 * this task - three TLS handshakes below internet_radio_start_track_chain() -
 * and the measured least-seen headroom fell to 1916 bytes, the thinnest of any
 * task on the board. A stack overflow here is a reboot, not a glitch. */
#define PLAYER_CONTROL_TASK_STACK_SIZE 10240U
#define PLAYER_CONTROL_TASK_PRIORITY 5U

static const char *TAG = "player_control";

static QueueHandle_t s_command_queue;
static audio_source_manager_t s_manager;
static atomic_int s_active_source = ATOMIC_VAR_INIT(AUDIO_SOURCE_NONE);
// Which listing row the USB player is on. Unlike the radio, the player itself
// has no notion of an index - it only knows a path - so the controller keeps it.
static atomic_size_t s_files_item_index = ATOMIC_VAR_INIT(PLAYER_ITEM_NONE);
// Bumped whenever the listing is replaced. The browser screen watches this to
// reset its cursor: the path itself is truncated to PLAYER_NAME_MAX_LEN in the
// snapshot, so two deep directories can look identical there.
static atomic_uint s_files_listing_revision = ATOMIC_VAR_INIT(0U);
/* Full path of the file being played. It is here rather than in the snapshot
 * because a path this long would not fit the 512-byte frame the snapshot is
 * diffed into for the web.
 *
 * Written by this task and read by the UI task, which writes the resume point
 * down, so it is held under a lock rather than by convention - a torn read
 * would be half of one path and half of another, and get saved as the file to
 * come back to. */
static char s_files_playing_path[FILE_BROWSER_PATH_MAX_LEN];
static SemaphoreHandle_t s_playing_path_lock;

static void player_set_playing_path(const char *path)
{
    if (s_playing_path_lock == NULL) return;
    xSemaphoreTake(s_playing_path_lock, portMAX_DELAY);
    snprintf(s_files_playing_path, sizeof(s_files_playing_path), "%s",
             path == NULL ? "" : path);
    xSemaphoreGive(s_playing_path_lock);
}

bool player_control_playing_file_path(char *out, size_t out_size)
{
    if (out == NULL || out_size == 0U || s_playing_path_lock == NULL) return false;
    xSemaphoreTake(s_playing_path_lock, portMAX_DELAY);
    const size_t length = strlen(s_files_playing_path);
    const bool fits = length < out_size;
    if (fits) memcpy(out, s_files_playing_path, length + 1U);
    xSemaphoreGive(s_playing_path_lock);
    return fits && length > 0U;
}
static atomic_bool s_rssi_seen = ATOMIC_VAR_INIT(false);
static atomic_uint s_rssi_updated_ms = ATOMIC_VAR_INIT(0U);
static atomic_bool s_rssi_valid = ATOMIC_VAR_INIT(false);
static atomic_int s_rssi_dbm = ATOMIC_VAR_INIT(0);
static atomic_flag s_rssi_refreshing = ATOMIC_FLAG_INIT;

static void player_refresh_rssi_if_due(void)
{
    const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    bool seen = atomic_load_explicit(&s_rssi_seen, memory_order_acquire);
    uint32_t updated_ms = atomic_load_explicit(&s_rssi_updated_ms,
                                               memory_order_relaxed);
    if (!player_rssi_refresh_due(seen, updated_ms, now_ms) ||
        atomic_flag_test_and_set_explicit(&s_rssi_refreshing,
                                          memory_order_acquire)) {
        return;
    }

    seen = atomic_load_explicit(&s_rssi_seen, memory_order_acquire);
    updated_ms = atomic_load_explicit(&s_rssi_updated_ms, memory_order_relaxed);
    if (player_rssi_refresh_due(seen, updated_ms, now_ms)) {
        int8_t rssi_dbm = 0;
        const bool valid = wifi_provisioning_get_rssi(&rssi_dbm);
        atomic_store_explicit(&s_rssi_dbm, rssi_dbm, memory_order_relaxed);
        atomic_store_explicit(&s_rssi_valid, valid, memory_order_release);
        atomic_store_explicit(&s_rssi_updated_ms, now_ms, memory_order_relaxed);
        atomic_store_explicit(&s_rssi_seen, true, memory_order_release);
    }
    atomic_flag_clear_explicit(&s_rssi_refreshing, memory_order_release);
}

// Selecting a directory browses into it instead of playing; selecting a file
// starts it. Both arrive as PLAYER_COMMAND_SELECT_ITEM because the cursor does
// not know which kind of entry it is on until the listing is consulted.
/* `playback_state` is what the snapshot said when the command was taken off
 * the queue; it is the only thing here that decides whether the file under the
 * cursor is already running. Passed in rather than read again so that the
 * whole of one command is decided from one view of the player. */
static void player_file_select_item(size_t index, player_playback_state_t playback_state)
{
    file_browser_entry_t entry;
    char path[FILE_BROWSER_PATH_MAX_LEN];
    // Name and path together, so a listing replaced mid-selection cannot pair
    // one directory's file with another directory's path.
    if (!file_storage_entry_path(index, &entry, path, sizeof(path))) {
        ESP_LOGW(TAG, "usb item %u is not in the current listing", (unsigned int)index);
        return;
    }
    if (entry.kind == FILE_BROWSER_ENTRY_DIRECTORY) {
        const esp_err_t result = file_storage_read_directory(path);
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "cannot open %s: %s", path, esp_err_to_name(result));
            return;
        }
        // The listing under the cursor is gone, so the remembered row would
        // point at an unrelated file in the new directory.
        atomic_store_explicit(&s_files_item_index, PLAYER_ITEM_NONE, memory_order_release);
        atomic_fetch_add_explicit(&s_files_listing_revision, 1U, memory_order_release);
        return;
    }
    /* Choosing the file that is already playing is how the user gets back to
     * the player screen from the browser, so it must not start the track over.
     * The row is remembered again on the way out: browsing clears it, and
     * without it the list would not mark the playing file and the track that
     * ends would have no position to advance from. */
    if (player_file_same_file_playing(path, s_files_playing_path, playback_state)) {
        atomic_store_explicit(&s_files_item_index, index, memory_order_release);
        return;
    }
    const esp_err_t result = file_player_play(path, entry.name, entry.format);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "cannot play %s: %s", path, esp_err_to_name(result));
        player_set_playing_path("");
        return;
    }
    player_set_playing_path(path);
    atomic_store_explicit(&s_files_item_index, index, memory_order_release);
}

/* Puts the browser back on the file that is playing.
 *
 * Browsing moves the listing wherever the user goes, including out of the
 * directory the current track lives in, and nothing ever moved it back - so
 * once the idle timeout returned to the player screen, the next trip into the
 * browser opened on a directory unrelated to what was coming out of the
 * speakers.
 *
 * The directory is reopened even when the listing never left it, rather than
 * comparing paths first: the comparison needs a second 1 KB path buffer on a
 * task whose stack is already the tightest on the device, and the read costs
 * the same as stepping into any folder, which this task does on a keypress
 * anyway. */
static void player_file_reveal_playing(void)
{
    if (s_files_playing_path[0] == '\0') return;
    char parent[FILE_BROWSER_PATH_MAX_LEN];
    if (!file_browser_path_parent(s_files_playing_path, parent, sizeof(parent))) return;
    if (file_storage_read_directory(parent) != ESP_OK) {
        ESP_LOGW(TAG, "cannot reopen %s", parent);
        return;
    }
    const char *name = strrchr(s_files_playing_path, '/');
    name = name == NULL ? s_files_playing_path : name + 1;
    const size_t index = file_storage_find_entry(name);
    // The file can have been deleted from another machine since it was opened;
    // the directory is still the right one to show, just without a cursor to
    // put on the playing row.
    atomic_store_explicit(&s_files_item_index,
                          index < file_storage_entry_count() ? index : PLAYER_ITEM_NONE,
                          memory_order_release);
    atomic_fetch_add_explicit(&s_files_listing_revision, 1U, memory_order_release);
}

static void player_file_advance(void)
{
    const size_t played = atomic_load_explicit(&s_files_item_index, memory_order_acquire);
    // A track that was started before the listing changed leaves no usable
    // position to advance from, so stop rather than guess.
    if (played == PLAYER_ITEM_NONE) return;
    const size_t next = file_storage_next_file(played + 1U);
    if (next >= file_storage_entry_count()) {
        ESP_LOGI(TAG, "usb: last track in the directory, stopping");
        atomic_store_explicit(&s_files_item_index, PLAYER_ITEM_NONE, memory_order_release);
        player_set_playing_path("");
        return;
    }
    // A track that ended is no longer playing, so nothing here can be mistaken
    // for the file that is - but say so explicitly rather than relying on it.
    player_file_select_item(next, PLAYER_PLAYBACK_STOPPED);
}

/* The two track keys on a file source: the file before or after the one
 * playing, skipping the directories in between.
 *
 * Unlike the advance a finished track triggers, running out of files here
 * stops nothing - the key simply does not act. The end of a directory is a
 * fact about the list, and taking the music off because the user pressed
 * "next" at the last track would be a punishment for asking. */
static void player_file_step(bool forward)
{
    const size_t playing = atomic_load_explicit(&s_files_item_index, memory_order_acquire);
    // Same reason player_file_advance() refuses: a track started before the
    // listing changed has no position in this one to step from.
    if (playing == PLAYER_ITEM_NONE) return;
    const size_t target = forward ? file_storage_next_file(playing + 1U)
                                  : file_storage_previous_file(playing);
    if (target >= file_storage_entry_count()) {
        ESP_LOGI(TAG, "files: no %s track in this directory", forward ? "next" : "previous");
        return;
    }
    player_file_select_item(target, PLAYER_PLAYBACK_STOPPED);
}

static void player_file_browse_up(void)
{
    char current[FILE_BROWSER_PATH_MAX_LEN];
    char parent[FILE_BROWSER_PATH_MAX_LEN];
    if (!file_storage_current_path(current, sizeof(current)) ||
        !file_browser_path_parent(current, parent, sizeof(parent))) {
        // Already at the mount root: there is nothing above it to browse.
        return;
    }
    if (file_storage_read_directory(parent) != ESP_OK) return;
    atomic_store_explicit(&s_files_item_index, PLAYER_ITEM_NONE, memory_order_release);
    atomic_fetch_add_explicit(&s_files_listing_revision, 1U, memory_order_release);
}

// Runs on the USB playback task as it exits, so it may only hand the intent
// over to this task's queue - see file_player_set_finished_callback().
static void player_file_track_finished(void)
{
    const player_command_t command = {.kind = PLAYER_COMMAND_TRACK_FINISHED};
    if (!player_control_post(&command)) {
        ESP_LOGW(TAG, "track finished but the command queue is full");
    }
}

// Runs on the USB host task, which unmounts the filesystem the moment this
// returns - see usb_storage_set_media_removing_callback(). So unlike the track
// callback above, this one cannot hand the work to the queue: it has to have
// released the drive before it comes back.
//
// file_player_stop() is called unconditionally rather than only when USB is the
// active source. What matters is that nothing is left reading the drive, and
// stopping a player that is already stopped costs one atomic read - trusting
// this task's bookkeeping instead would turn any drift in it into a read of
// freed FATFS state.
/* Which row of the Yandex list is playing. The radio status cannot answer for
 * it: a chain of tracks has no index in the station catalog, and reports
 * SIZE_MAX on purpose so that a reconnect never tries to reopen it by one. */
static atomic_size_t s_yandex_item_index = ATOMIC_VAR_INIT(PLAYER_ITEM_NONE);

/* Starts the station on `index` of the Yandex list as a chain of tracks. The
 * first link is fetched inside internet_radio_start_track_chain(), so this
 * blocks for as long as three API calls take - acceptable on this task, which
 * is the one that already blocks on opening a station. */
static bool player_yandex_start(size_t index)
{
    yandex_station_t station;
    if (!yandex_catalog_station_at(index, &station)) {
        ESP_LOGW(TAG, "yandex station %u is gone from the list", (unsigned int)index);
        return false;
    }
    /* idForFrom travels with the station from the dashboard precisely so this
     * call does not have to fetch it again to name where the listening began. */
    if (yandex_rotor_start(station.id, station.from) != ESP_OK) {
        ESP_LOGE(TAG, "yandex rotor did not start for %s", station.id);
        return false;
    }
    if (!internet_radio_start_track_chain(station.name)) {
        return false;
    }
    atomic_store_explicit(&s_yandex_item_index, index, memory_order_release);
    return true;
}

static void player_file_media_removing(void)
{
    const esp_err_t result = file_player_stop();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "usb player did not release the drive: %s", esp_err_to_name(result));
    }
    // The drive that the cover came from is on its way out; leaving the
    // picture up would outlast the file it belongs to.
    album_art_clear();
    // The active source is deliberately left alone: it belongs to this task,
    // and the snapshot is rebuilt on demand from the player's own state and
    // usb_storage_media(), which both already say the drive is gone.
}

static bool player_stop_active_source(audio_source_t source)
{
    if (audio_source_is_stations(source)) {
        const esp_err_t result = internet_radio_stop();
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "internet radio stop failed: %s", esp_err_to_name(result));
            return false;
        }
        if (source == AUDIO_SOURCE_YANDEX) {
            /* After the decoder has stopped, so nothing is inside a call that
             * reads the rotor's buffer while it is being freed. The chain's
             * position is kept: coming back to the same station should not
             * replay what was already heard. */
            yandex_rotor_stop();
            /* The picture belonged to the track that just stopped; leaving it
             * up would outlast what it describes. */
            album_art_clear();
            atomic_store_explicit(&s_yandex_item_index, PLAYER_ITEM_NONE,
                                  memory_order_release);
        }
    } else if (audio_source_is_files(source)) {
        const esp_err_t result = file_player_stop();
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "file player stop failed: %s", esp_err_to_name(result));
            return false;
        }
        /* Only on a real stop, never between tracks: advancing goes straight
         * to the next play(), so a cover shared by a whole album stays on the
         * screen instead of being decoded again for every track. */
        album_art_clear();
        // Nothing is playing any more, so there is no directory to reveal.
        player_set_playing_path("");
        /* And the card goes back in its box. The order is the whole point:
         * file_player_stop() has returned, so nothing is inside fread() on the
         * volume this is about to tear down - the same contract the USB
         * removal callback has to honour, only here we choose the moment.
         *
         * Unmounting rather than holding it is what keeps the ~2.4 KB of
         * internal SRAM a mount costs out of the radio's way; see
         * sd_storage_mount(). */
        if (source == AUDIO_SOURCE_SD) {
            (void)sd_storage_unmount();
        }
    }

    const audio_source_result_t result = audio_source_manager_stop(&s_manager, source);
    if (result == AUDIO_SOURCE_OK) {
        atomic_store_explicit(&s_active_source, AUDIO_SOURCE_NONE, memory_order_release);
        return true;
    }
    ESP_LOGW(TAG, "audio source stop failed: source=%d result=%d", (int)source, (int)result);
    return false;
}

static void player_control_task(void *arg)
{
    (void)arg;
    audio_source_manager_init(&s_manager);

    player_command_t command;
    for (;;) {
        if (xQueueReceive(s_command_queue, &command, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        player_snapshot_t snapshot;
        player_control_get_snapshot(&snapshot);
        // Named rather than switched on directly: the two track keys share one
        // branch and have to ask which of them they are.
        const player_operation_t operation = player_control_decide(&snapshot, &command);
        switch (operation) {
        case PLAYER_OPERATION_SELECT_SOURCE: {
            // Re-selecting the same source (retry after an error) also lands
            // here, so the previous source must actually be stopped before
            // the manager is told a new one is starting; otherwise the
            // manager and s_active_source would disagree about what is
            // active, wedging future start attempts.
            bool ready_to_start = snapshot.active_source == AUDIO_SOURCE_NONE;
            if (!ready_to_start) {
                ready_to_start = player_stop_active_source(snapshot.active_source);
            }
            if (!ready_to_start) {
                ESP_LOGW(TAG, "select source deferred: previous source did not stop");
                break;
            }
            if (audio_source_manager_start(&s_manager, command.source) != AUDIO_SOURCE_OK) {
                ESP_LOGW(TAG, "audio source start failed: source=%d", (int)command.source);
                break;
            }
            atomic_store_explicit(&s_active_source, command.source, memory_order_release);
            /* Selecting a source never starts anything, the radio included.
             * It used to resume the last station here, which made every
             * arrival at the station list start playing whatever was on last -
             * the user was picking a station and the box had already chosen
             * one. Autoplay wants that resume, and asks for it by posting
             * PLAY after this; nothing else does. */
            if (audio_source_is_files(command.source)) {
                /* The card is mounted here and nowhere else. It is the only
                 * moment that can: nothing detects it being inserted, so the
                 * user asking for it *is* the detection - and mounting costs
                 * internal SRAM that the radio wants back the moment this
                 * source is left again. */
                if (command.source == AUDIO_SOURCE_SD) {
                    const esp_err_t mounted = sd_storage_mount();
                    if (mounted != ESP_OK) {
                        // Not an error to recover from here: the snapshot now
                        // reports what the slot holds, and the browser screen
                        // turns that into a line the user can act on.
                        ESP_LOGW(TAG, "sd card not available: %s", esp_err_to_name(mounted));
                    }
                }
                /* Selecting the source only opens the volume's root; nothing
                 * plays until an entry is chosen, because there is no
                 * equivalent of the radio's "last station".
                 *
                 * A root that will not open leaves the listing pointing at it
                 * and empty, rather than at whatever was browsed last: the
                 * browser screen would otherwise show the drive's files under
                 * the card's name. */
                const char *root = command.source == AUDIO_SOURCE_SD
                                       ? SD_STORAGE_ROOT_PATH
                                       : USB_STORAGE_ROOT_PATH;
                if (file_storage_read_directory(root) != ESP_OK) {
                    file_storage_open_empty(root);
                }
                atomic_store_explicit(&s_files_item_index, PLAYER_ITEM_NONE,
                                      memory_order_release);
                atomic_fetch_add_explicit(&s_files_listing_revision, 1U, memory_order_release);
            }
            break;
        }
        case PLAYER_OPERATION_START_ITEM:
            if (audio_source_is_files(snapshot.active_source)) {
                player_file_select_item(command.item_index, snapshot.playback_state);
            } else if (snapshot.active_source == AUDIO_SOURCE_YANDEX) {
                (void)player_yandex_start(command.item_index);
            } else {
                (void)internet_radio_start_station_index(command.item_index);
            }
            break;
        case PLAYER_OPERATION_START_SAVED:
            if (snapshot.active_source == AUDIO_SOURCE_YANDEX) {
                /* There is no saved Yandex station, and the radio's saved one
                 * belongs to a different source: play again means the station
                 * that was last chosen here, and nothing at all before that. */
                const size_t index =
                    atomic_load_explicit(&s_yandex_item_index, memory_order_acquire);
                if (index != PLAYER_ITEM_NONE) (void)player_yandex_start(index);
            } else if (!audio_source_is_files(snapshot.active_source)) {
                (void)internet_radio_start_saved_station();
            }
            break;
        case PLAYER_OPERATION_PAUSE:
            if (audio_source_is_files(snapshot.active_source)) {
                (void)file_player_pause();
            } else {
                (void)internet_radio_pause();
            }
            break;
        case PLAYER_OPERATION_RESUME:
            if (audio_source_is_files(snapshot.active_source)) {
                (void)file_player_resume();
            } else {
                (void)internet_radio_resume();
            }
            break;
        case PLAYER_OPERATION_SEEK:
            // Nothing is resumed here: the file plays throughout scrubbing, so
            // a jump is only a jump. Asked for on a paused file - which only
            // the web UI can arrange - the request waits and lands on the pass
            // that resumes it.
            (void)file_player_seek(command.position_seconds);
            break;
        case PLAYER_OPERATION_NEXT_TRACK:
            /* Told to the rotor before the skip is asked for: the decode task
             * acts on it almost at once, and a track left early has to be
             * reported as rejected rather than as heard to the end. */
            if (snapshot.active_source == AUDIO_SOURCE_YANDEX) {
                yandex_rotor_note_skip();
            }
            if (!internet_radio_skip_track()) {
                ESP_LOGW(TAG, "nothing to skip");
            }
            break;
        case PLAYER_OPERATION_ADVANCE_ITEM:
            player_file_advance();
            break;
        case PLAYER_OPERATION_PREVIOUS_ITEM:
        case PLAYER_OPERATION_NEXT_ITEM: {
            const bool forward = operation == PLAYER_OPERATION_NEXT_ITEM;
            if (audio_source_is_files(snapshot.active_source)) {
                player_file_step(forward);
            } else {
                /* The station either side of the one playing. The bounds were
                 * checked by player_control_decide(), which had the same
                 * count in front of it - and starting an index the catalog no
                 * longer holds is refused below anyway. */
                const size_t target = forward ? snapshot.active_item_index + 1U
                                              : snapshot.active_item_index - 1U;
                (void)internet_radio_start_station_index(target);
            }
            break;
        }
        case PLAYER_OPERATION_TOGGLE_LIKE:
        case PLAYER_OPERATION_TOGGLE_DISLIKE: {
            /* Blocks on one POST, on the task that already blocks on three of
             * them to open a station. The mark is only moved once the API has
             * taken it: a heart that fills in and then quietly disagrees with
             * the phone is worse than one that does not move.
             *
             * One request even when the marks cross. The two lists exclude
             * each other server-side in both directions - measured by reading
             * both libraries around each call - so disliking a liked track
             * needs no second POST to take the like off, and the rotor's copy
             * of the marks follows the same rule. */
            const bool dislike = operation == PLAYER_OPERATION_TOGGLE_DISLIKE;
            char track_id[YANDEX_TRACK_ID_MAX + 1U];
            bool liked = false;
            bool disliked = false;
            if (!yandex_rotor_playing_track(track_id, sizeof(track_id), &liked, &disliked)) {
                ESP_LOGW(TAG, "nothing playing to mark");
                break;
            }
            const bool set = dislike ? !disliked : !liked;
            if (yandex_likes_set(track_id, dislike ? YANDEX_MARK_DISLIKE : YANDEX_MARK_LIKE,
                                 set) != ESP_OK) {
                break;
            }
            if (dislike) yandex_rotor_set_playing_disliked(set);
            else yandex_rotor_set_playing_liked(set);
            break;
        }
        case PLAYER_OPERATION_BROWSE_UP:
            player_file_browse_up();
            break;
        case PLAYER_OPERATION_BROWSE_REVEAL:
            player_file_reveal_playing();
            break;
        case PLAYER_OPERATION_STOP:
            (void)player_stop_active_source(snapshot.active_source);
            break;
        case PLAYER_OPERATION_NONE:
            break;
        case PLAYER_OPERATION_INVALID:
            ESP_LOGW(TAG, "invalid player command kind=%d", (int)command.kind);
            break;
        }
    }
}

esp_err_t player_control_init(void)
{
    if (s_command_queue != NULL) {
        return ESP_OK;
    }

    atomic_store_explicit(&s_active_source, AUDIO_SOURCE_NONE, memory_order_release);
    atomic_store_explicit(&s_rssi_seen, false, memory_order_release);
    atomic_store_explicit(&s_rssi_updated_ms, 0U, memory_order_relaxed);
    atomic_store_explicit(&s_rssi_valid, false, memory_order_release);
    atomic_store_explicit(&s_rssi_dbm, 0, memory_order_relaxed);
    atomic_flag_clear_explicit(&s_rssi_refreshing, memory_order_release);
    s_playing_path_lock = xSemaphoreCreateMutex();
    if (s_playing_path_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_command_queue = xQueueCreate(PLAYER_COMMAND_QUEUE_LENGTH,
                                   sizeof(player_command_t));
    if (s_command_queue == NULL) {
        vSemaphoreDelete(s_playing_path_lock);
        s_playing_path_lock = NULL;
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(player_control_task, "player_control",
                    PLAYER_CONTROL_TASK_STACK_SIZE, NULL,
                    PLAYER_CONTROL_TASK_PRIORITY, NULL) != pdPASS) {
        vQueueDelete(s_command_queue);
        vSemaphoreDelete(s_playing_path_lock);
        s_command_queue = NULL;
        s_playing_path_lock = NULL;
        return ESP_ERR_NO_MEM;
    }
    file_player_set_finished_callback(player_file_track_finished);
    usb_storage_set_media_removing_callback(player_file_media_removing);
    return ESP_OK;
}

bool player_control_post(const player_command_t *command)
{
    return command != NULL && s_command_queue != NULL &&
           xQueueSend(s_command_queue, command, 0) == pdTRUE;
}

void player_control_get_snapshot(player_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    /* What the build has, before what the device currently sees. A part that
     * is not on this board must not be startable at all - not from the home
     * screen, which already leaves its row out, and not from the web or from
     * autoplay either, both of which come through here. player_control_decide()
     * refuses a source whose bit is missing, so this one test covers all
     * three. */
    snapshot->capabilities = PLAYER_CAP_INTERNET_RADIO;
    snapshot->usb_media = usb_storage_media();
    snapshot->sd_media = sd_storage_media();
    snapshot->listing_revision = player_control_listing_revision();
    snapshot->files_entry_count = file_storage_entry_count();
    if (BOARD_HAS_USB && usb_storage_is_mounted()) {
        snapshot->capabilities |= PLAYER_CAP_USB;
    }
    // Always, when the socket exists: see PLAYER_CAP_SD. What is actually in
    // the slot is sd_media.
    if (BOARD_HAS_SD_CARD) {
        snapshot->capabilities |= PLAYER_CAP_SD;
    }
    if (BOARD_HAS_YANDEX_MUSIC && yandex_auth_is_authorized()) {
        snapshot->capabilities |= PLAYER_CAP_YANDEX;
    }
    snapshot->active_source =
        (audio_source_t)atomic_load_explicit(&s_active_source, memory_order_acquire);

    /* Filled for every source, not just the radio. It used to live in the
     * radio branch on the grounds that nothing on the USB screen depends on
     * the network - but the player screen now carries one status strip for
     * both, and a strip reporting no signal while the device is plainly
     * connected is worse than one reporting nothing at all. */
    player_refresh_rssi_if_due();
    snapshot->wifi_rssi_valid =
        atomic_load_explicit(&s_rssi_valid, memory_order_acquire);
    snapshot->wifi_rssi_dbm =
        (int8_t)atomic_load_explicit(&s_rssi_dbm, memory_order_relaxed);

    if (audio_source_is_files(snapshot->active_source)) {
        file_player_status_t usb_status;
        file_player_get_status(&usb_status);
        snapshot->playback_state = player_playback_from_file(usb_status.state);
        snapshot->active_item_index =
            atomic_load_explicit(&s_files_item_index, memory_order_acquire);
        snapshot->item_count = file_storage_entry_count();
        // The directory being browsed takes the place the radio gives the
        // station name, and the file name the place of the ICY title.
        (void)file_storage_current_path(snapshot->context, sizeof(snapshot->context));
        snprintf(snapshot->stream_title, sizeof(snapshot->stream_title), "%s",
                 usb_status.track);
        snprintf(snapshot->codec, sizeof(snapshot->codec), "%s", usb_status.codec);
        snapshot->bitrate_kbps = usb_status.bitrate_kbps;
        snapshot->sample_rate_hz = usb_status.sample_rate_hz;
        if (usb_status.state == FILE_PLAYER_STATE_ERROR) {
            snprintf(snapshot->error, sizeof(snapshot->error), "Не удалось воспроизвести файл");
        }
        return;
    }

    internet_radio_status_t radio_status;
    internet_radio_get_status(&radio_status);
    snapshot->playback_state = player_playback_from_radio(radio_status.state);
    if (snapshot->active_source == AUDIO_SOURCE_YANDEX) {
        /* The rotor's list, and the row this task remembers starting: the
         * radio status reports SIZE_MAX for a chain by design. */
        snapshot->active_item_index =
            atomic_load_explicit(&s_yandex_item_index, memory_order_acquire);
        snapshot->item_count = yandex_catalog_count();
        /* The like mark, which only this source has: a station and a file
         * belong to nobody's library. It travels in the snapshot because both
         * screens that draw it - the device's and the web's - already poll
         * for one, and it changes about as often as the track does. */
        char track_id[YANDEX_TRACK_ID_MAX + 1U];
        bool liked = false;
        bool disliked = false;
        if (yandex_rotor_playing_track(track_id, sizeof(track_id), &liked, &disliked)) {
            snapshot->track_likeable = true;
            snapshot->track_liked = liked;
            snapshot->track_disliked = disliked;
        }
    } else {
        snapshot->active_item_index = radio_status.station_index;
        snapshot->item_count = internet_radio_station_count();
    }
    snprintf(snapshot->context, sizeof(snapshot->context), "%s", radio_status.station);
    snprintf(snapshot->stream_title, sizeof(snapshot->stream_title), "%s",
             radio_status.title);
    snprintf(snapshot->codec, sizeof(snapshot->codec), "%s", radio_status.codec);
    snapshot->bitrate_kbps = radio_status.bitrate_kbps;
    snapshot->sample_rate_hz = radio_status.sample_rate_hz;
    if (radio_status.state == INTERNET_RADIO_STATE_ERROR) {
        /* An account without an active subscription gets preview variants only,
         * and that is worth saying: everything else about it looks exactly
         * like a station that would not open. */
        if (snapshot->active_source == AUDIO_SOURCE_YANDEX &&
            yandex_rotor_last_error() == ESP_ERR_NOT_SUPPORTED) {
            snprintf(snapshot->error, sizeof(snapshot->error), "Нужна подписка Яндекс Музыки");
        } else {
            snprintf(snapshot->error, sizeof(snapshot->error),
                     "Не удалось подключиться к станции");
        }
    }
}

bool player_control_input_fill(uint8_t *percent)
{
    if (percent == NULL) return false;
    const audio_source_t source =
        (audio_source_t)atomic_load_explicit(&s_active_source, memory_order_acquire);
    uint8_t reported;
    if (audio_source_is_files(source)) {
        file_player_status_t status;
        file_player_get_status(&status);
        if (status.state != FILE_PLAYER_STATE_PLAYING &&
            status.state != FILE_PLAYER_STATE_PAUSED) {
            return false;
        }
        reported = status.buffer_percent;
    } else if (audio_source_is_stations(source)) {
        internet_radio_status_t status;
        internet_radio_get_status(&status);
        if (status.state != INTERNET_RADIO_STATE_PLAYING &&
            status.state != INTERNET_RADIO_STATE_PAUSED) {
            return false;
        }
        reported = status.buffer_percent;
    } else {
        return false;
    }
    if (reported == RADIO_PREBUFFER_PERCENT_NONE) return false;
    *percent = reported;
    return true;
}

bool player_control_track_progress(uint32_t *elapsed_seconds, uint32_t *total_seconds)
{
    if (elapsed_seconds == NULL || total_seconds == NULL) return false;
    const audio_source_t source =
        (audio_source_t)atomic_load_explicit(&s_active_source, memory_order_acquire);
    if (!audio_source_is_files(source)) return false;

    file_player_status_t status;
    file_player_get_status(&status);
    if (status.state != FILE_PLAYER_STATE_PLAYING && status.state != FILE_PLAYER_STATE_PAUSED) {
        return false;
    }
    *elapsed_seconds = status.elapsed_seconds;
    *total_seconds = status.total_seconds;
    return true;
}

bool player_control_track_tags(audio_tags_t *tags)
{
    if (tags == NULL) return false;
    audio_tags_clear(tags);
    const audio_source_t source =
        (audio_source_t)atomic_load_explicit(&s_active_source, memory_order_acquire);
    if (!audio_source_is_files(source)) return false;

    file_player_get_tags(tags);
    // False for an untagged file, so the caller falls back to the file name
    // rather than clearing three rows it has nothing to put in. The player
    // clears these when it starts a track, so they never outlive their file.
    return audio_tags_have_text(tags);
}

const station_catalog_entry_t *player_control_station_at(size_t index)
{
    return internet_radio_station_at(index);
}

bool player_control_file_entry_at(size_t index, file_browser_entry_t *entry)
{
    return file_storage_entry_at(index, entry);
}

bool player_control_file_resume_path(const char *path)
{
    char parent[FILE_BROWSER_PATH_MAX_LEN];
    if (path == NULL || path[0] == '\0') return false;
    if (!file_browser_path_parent(path, parent, sizeof(parent))) return false;
    /* Which volume the remembered path is on is in the path itself. The card
     * has to be mounted before it can be searched, and this is the one caller
     * that reaches it without going through a source selection - autoplay at
     * boot resumes the file directly. Mounting is idempotent, so a card that
     * a select has already brought up costs nothing here. */
    if (strncmp(path, SD_STORAGE_ROOT_PATH "/", sizeof(SD_STORAGE_ROOT_PATH)) == 0) {
        (void)sd_storage_mount();
    }
    if (!file_storage_path_mounted(path)) return false;

    /* Open the directory first: the remembered name is only meaningful inside
     * it, and if that fails there is nothing to search. */
    if (file_storage_read_directory(parent) != ESP_OK) {
        // Back to the volume's own root, whichever it is: the browser has to
        // open somewhere, and the root is the one directory always there.
        char root[FILE_BROWSER_PATH_MAX_LEN];
        (void)file_browser_path_volume_root(path, root, sizeof(root));
        (void)file_storage_read_directory(root);
        atomic_fetch_add_explicit(&s_files_listing_revision, 1U, memory_order_release);
        return false;
    }
    atomic_fetch_add_explicit(&s_files_listing_revision, 1U, memory_order_release);

    const char *name = strrchr(path, '/');
    name = name == NULL ? path : name + 1;
    const size_t index = file_storage_find_entry(name);
    if (index >= file_storage_entry_count()) return false;
    file_browser_entry_t entry;
    /* A directory that took the file's name is not the track we remembered. */
    if (!file_storage_entry_at(index, &entry) ||
        entry.kind == FILE_BROWSER_ENTRY_DIRECTORY) {
        return false;
    }
    // Nothing is playing at start-up, so this can only be a fresh start.
    player_file_select_item(index, PLAYER_PLAYBACK_STOPPED);
    return true;
}

unsigned int player_control_listing_revision(void)
{
    return atomic_load_explicit(&s_files_listing_revision, memory_order_acquire);
}
