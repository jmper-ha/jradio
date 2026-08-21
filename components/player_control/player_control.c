#include "player_control.h"

#include "radio_prebuffer.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "album_art.h"
#include "audio_source.h"
#include "internet_radio.h"
#include "usb_browser.h"
#include "usb_player.h"
#include "usb_storage.h"
#include "wifi_provisioning.h"

#define PLAYER_COMMAND_QUEUE_LENGTH 8U
/* Measured, not guessed. The deep path is not playback but browsing: opening a
 * directory runs on this task and descends through FATFS into the MSC
 * transfers, and it left 1368 bytes of 6144. That is the thinnest margin in the
 * system, and this is the task that overflowed the one time the build was
 * compiled at -O2, where inlining costs more stack again. */
#define PLAYER_CONTROL_TASK_STACK_SIZE 8192U
#define PLAYER_CONTROL_TASK_PRIORITY 5U

static const char *TAG = "player_control";

static QueueHandle_t s_command_queue;
static audio_source_manager_t s_manager;
static atomic_int s_active_source = ATOMIC_VAR_INIT(AUDIO_SOURCE_NONE);
// Which listing row the USB player is on. Unlike the radio, the player itself
// has no notion of an index - it only knows a path - so the controller keeps it.
static atomic_size_t s_usb_item_index = ATOMIC_VAR_INIT(PLAYER_ITEM_NONE);
// Bumped whenever the listing is replaced. The browser screen watches this to
// reset its cursor: the path itself is truncated to PLAYER_NAME_MAX_LEN in the
// snapshot, so two deep directories can look identical there.
static atomic_uint s_usb_listing_revision = ATOMIC_VAR_INIT(0U);
/* Full path of the file the drive is playing. Written and read only by this
 * task, so it needs no atomics; it is here rather than in the snapshot because
 * the browser wants it once, when it opens, and a path this long would not fit
 * the 512-byte frame the snapshot is diffed into for the web. */
static char s_usb_playing_path[USB_BROWSER_PATH_MAX_LEN];
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
static void player_usb_select_item(size_t index)
{
    usb_browser_entry_t entry;
    char path[USB_BROWSER_PATH_MAX_LEN];
    // Name and path together, so a listing replaced mid-selection cannot pair
    // one directory's file with another directory's path.
    if (!usb_storage_entry_path(index, &entry, path, sizeof(path))) {
        ESP_LOGW(TAG, "usb item %u is not in the current listing", (unsigned int)index);
        return;
    }
    if (entry.kind == USB_BROWSER_ENTRY_DIRECTORY) {
        const esp_err_t result = usb_storage_read_directory(path);
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "cannot open %s: %s", path, esp_err_to_name(result));
            return;
        }
        // The listing under the cursor is gone, so the remembered row would
        // point at an unrelated file in the new directory.
        atomic_store_explicit(&s_usb_item_index, PLAYER_ITEM_NONE, memory_order_release);
        atomic_fetch_add_explicit(&s_usb_listing_revision, 1U, memory_order_release);
        return;
    }
    const esp_err_t result = usb_player_play(path, entry.name, entry.format);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "cannot play %s: %s", path, esp_err_to_name(result));
        s_usb_playing_path[0] = '\0';
        return;
    }
    snprintf(s_usb_playing_path, sizeof(s_usb_playing_path), "%s", path);
    atomic_store_explicit(&s_usb_item_index, index, memory_order_release);
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
static void player_usb_reveal_playing(void)
{
    if (s_usb_playing_path[0] == '\0') return;
    char parent[USB_BROWSER_PATH_MAX_LEN];
    if (!usb_browser_path_parent(s_usb_playing_path, parent, sizeof(parent))) return;
    if (usb_storage_read_directory(parent) != ESP_OK) {
        ESP_LOGW(TAG, "cannot reopen %s", parent);
        return;
    }
    const char *name = strrchr(s_usb_playing_path, '/');
    name = name == NULL ? s_usb_playing_path : name + 1;
    const size_t index = usb_storage_find_entry(name);
    // The file can have been deleted from another machine since it was opened;
    // the directory is still the right one to show, just without a cursor to
    // put on the playing row.
    atomic_store_explicit(&s_usb_item_index,
                          index < usb_storage_entry_count() ? index : PLAYER_ITEM_NONE,
                          memory_order_release);
    atomic_fetch_add_explicit(&s_usb_listing_revision, 1U, memory_order_release);
}

static void player_usb_advance(void)
{
    const size_t played = atomic_load_explicit(&s_usb_item_index, memory_order_acquire);
    // A track that was started before the listing changed leaves no usable
    // position to advance from, so stop rather than guess.
    if (played == PLAYER_ITEM_NONE) return;
    const size_t next = usb_storage_next_file(played + 1U);
    if (next >= usb_storage_entry_count()) {
        ESP_LOGI(TAG, "usb: last track in the directory, stopping");
        atomic_store_explicit(&s_usb_item_index, PLAYER_ITEM_NONE, memory_order_release);
        s_usb_playing_path[0] = '\0';
        return;
    }
    player_usb_select_item(next);
}

static void player_usb_browse_up(void)
{
    char current[USB_BROWSER_PATH_MAX_LEN];
    char parent[USB_BROWSER_PATH_MAX_LEN];
    if (!usb_storage_current_path(current, sizeof(current)) ||
        !usb_browser_path_parent(current, parent, sizeof(parent))) {
        // Already at the mount root: there is nothing above it to browse.
        return;
    }
    if (usb_storage_read_directory(parent) != ESP_OK) return;
    atomic_store_explicit(&s_usb_item_index, PLAYER_ITEM_NONE, memory_order_release);
    atomic_fetch_add_explicit(&s_usb_listing_revision, 1U, memory_order_release);
}

// Runs on the USB playback task as it exits, so it may only hand the intent
// over to this task's queue - see usb_player_set_finished_callback().
static void player_usb_track_finished(void)
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
// usb_player_stop() is called unconditionally rather than only when USB is the
// active source. What matters is that nothing is left reading the drive, and
// stopping a player that is already stopped costs one atomic read - trusting
// this task's bookkeeping instead would turn any drift in it into a read of
// freed FATFS state.
static void player_usb_media_removing(void)
{
    const esp_err_t result = usb_player_stop();
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
    if (source == AUDIO_SOURCE_INTERNET_RADIO) {
        const esp_err_t result = internet_radio_stop();
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "internet radio stop failed: %s", esp_err_to_name(result));
            return false;
        }
    } else if (source == AUDIO_SOURCE_USB) {
        const esp_err_t result = usb_player_stop();
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "usb player stop failed: %s", esp_err_to_name(result));
            return false;
        }
        /* Only on a real stop, never between tracks: advancing goes straight
         * to the next play(), so a cover shared by a whole album stays on the
         * screen instead of being decoded again for every track. */
        album_art_clear();
        // Nothing is playing any more, so there is no directory to reveal.
        s_usb_playing_path[0] = '\0';
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
        switch (player_control_decide(&snapshot, &command)) {
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
            if (command.source == AUDIO_SOURCE_INTERNET_RADIO) {
                (void)internet_radio_start_saved_station();
            } else if (command.source == AUDIO_SOURCE_USB) {
                // Selecting the source only opens the drive's root; nothing
                // plays until an entry is chosen, because there is no
                // equivalent of the radio's "last station".
                (void)usb_storage_read_directory(USB_BROWSER_ROOT_PATH);
                atomic_store_explicit(&s_usb_item_index, PLAYER_ITEM_NONE,
                                      memory_order_release);
                atomic_fetch_add_explicit(&s_usb_listing_revision, 1U, memory_order_release);
            }
            break;
        }
        case PLAYER_OPERATION_START_ITEM:
            if (snapshot.active_source == AUDIO_SOURCE_USB) {
                player_usb_select_item(command.item_index);
            } else {
                (void)internet_radio_start_station_index(command.item_index);
            }
            break;
        case PLAYER_OPERATION_START_SAVED:
            if (snapshot.active_source != AUDIO_SOURCE_USB) {
                (void)internet_radio_start_saved_station();
            }
            break;
        case PLAYER_OPERATION_PAUSE:
            if (snapshot.active_source == AUDIO_SOURCE_USB) {
                (void)usb_player_pause();
            } else {
                (void)internet_radio_pause();
            }
            break;
        case PLAYER_OPERATION_RESUME:
            if (snapshot.active_source == AUDIO_SOURCE_USB) {
                (void)usb_player_resume();
            } else {
                (void)internet_radio_resume();
            }
            break;
        case PLAYER_OPERATION_ADVANCE_ITEM:
            player_usb_advance();
            break;
        case PLAYER_OPERATION_BROWSE_UP:
            player_usb_browse_up();
            break;
        case PLAYER_OPERATION_BROWSE_REVEAL:
            player_usb_reveal_playing();
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
    s_command_queue = xQueueCreate(PLAYER_COMMAND_QUEUE_LENGTH,
                                   sizeof(player_command_t));
    if (s_command_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(player_control_task, "player_control",
                    PLAYER_CONTROL_TASK_STACK_SIZE, NULL,
                    PLAYER_CONTROL_TASK_PRIORITY, NULL) != pdPASS) {
        vQueueDelete(s_command_queue);
        s_command_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    usb_player_set_finished_callback(player_usb_track_finished);
    usb_storage_set_media_removing_callback(player_usb_media_removing);
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
    snapshot->capabilities = PLAYER_CAP_INTERNET_RADIO;
    snapshot->usb_media = usb_storage_media();
    snapshot->usb_listing_revision = player_control_usb_listing_revision();
    snapshot->usb_entry_count = usb_storage_entry_count();
    if (usb_storage_is_mounted()) {
        snapshot->capabilities |= PLAYER_CAP_USB;
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

    if (snapshot->active_source == AUDIO_SOURCE_USB) {
        usb_player_status_t usb_status;
        usb_player_get_status(&usb_status);
        snapshot->playback_state = player_playback_from_usb(usb_status.state);
        snapshot->active_item_index =
            atomic_load_explicit(&s_usb_item_index, memory_order_acquire);
        snapshot->item_count = usb_storage_entry_count();
        // The directory being browsed takes the place the radio gives the
        // station name, and the file name the place of the ICY title.
        (void)usb_storage_current_path(snapshot->context, sizeof(snapshot->context));
        snprintf(snapshot->stream_title, sizeof(snapshot->stream_title), "%s",
                 usb_status.track);
        snprintf(snapshot->codec, sizeof(snapshot->codec), "%s", usb_status.codec);
        snapshot->bitrate_kbps = usb_status.bitrate_kbps;
        snapshot->sample_rate_hz = usb_status.sample_rate_hz;
        if (usb_status.state == USB_PLAYER_STATE_ERROR) {
            snprintf(snapshot->error, sizeof(snapshot->error), "Не удалось воспроизвести файл");
        }
        return;
    }

    internet_radio_status_t radio_status;
    internet_radio_get_status(&radio_status);
    snapshot->playback_state = player_playback_from_radio(radio_status.state);
    snapshot->active_item_index = radio_status.station_index;
    snapshot->item_count = internet_radio_station_count();
    snprintf(snapshot->context, sizeof(snapshot->context), "%s", radio_status.station);
    snprintf(snapshot->stream_title, sizeof(snapshot->stream_title), "%s",
             radio_status.title);
    snprintf(snapshot->codec, sizeof(snapshot->codec), "%s", radio_status.codec);
    snapshot->bitrate_kbps = radio_status.bitrate_kbps;
    snapshot->sample_rate_hz = radio_status.sample_rate_hz;
    if (radio_status.state == INTERNET_RADIO_STATE_ERROR) {
        snprintf(snapshot->error, sizeof(snapshot->error),
                 "Не удалось подключиться к станции");
    }
}

bool player_control_input_fill(uint8_t *percent)
{
    if (percent == NULL) return false;
    const audio_source_t source =
        (audio_source_t)atomic_load_explicit(&s_active_source, memory_order_acquire);
    uint8_t reported;
    if (source == AUDIO_SOURCE_USB) {
        usb_player_status_t status;
        usb_player_get_status(&status);
        if (status.state != USB_PLAYER_STATE_PLAYING &&
            status.state != USB_PLAYER_STATE_PAUSED) {
            return false;
        }
        reported = status.buffer_percent;
    } else if (source == AUDIO_SOURCE_INTERNET_RADIO) {
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
    if (source != AUDIO_SOURCE_USB) return false;

    usb_player_status_t status;
    usb_player_get_status(&status);
    if (status.state != USB_PLAYER_STATE_PLAYING && status.state != USB_PLAYER_STATE_PAUSED) {
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
    if (source != AUDIO_SOURCE_USB) return false;

    usb_player_get_tags(tags);
    // False for an untagged file, so the caller falls back to the file name
    // rather than clearing three rows it has nothing to put in. The player
    // clears these when it starts a track, so they never outlive their file.
    return audio_tags_have_text(tags);
}

const station_catalog_entry_t *player_control_station_at(size_t index)
{
    return internet_radio_station_at(index);
}

bool player_control_usb_entry_at(size_t index, usb_browser_entry_t *entry)
{
    return usb_storage_entry_at(index, entry);
}

bool player_control_usb_resume_path(const char *path)
{
    char parent[USB_BROWSER_PATH_MAX_LEN];
    if (path == NULL || path[0] == '\0' || !usb_storage_is_mounted()) return false;
    if (!usb_browser_path_parent(path, parent, sizeof(parent))) return false;

    /* Open the directory first: the remembered name is only meaningful inside
     * it, and if that fails there is nothing to search. */
    if (usb_storage_read_directory(parent) != ESP_OK) {
        (void)usb_storage_read_directory(USB_BROWSER_ROOT_PATH);
        atomic_fetch_add_explicit(&s_usb_listing_revision, 1U, memory_order_release);
        return false;
    }
    atomic_fetch_add_explicit(&s_usb_listing_revision, 1U, memory_order_release);

    const char *name = strrchr(path, '/');
    name = name == NULL ? path : name + 1;
    const size_t index = usb_storage_find_entry(name);
    if (index >= usb_storage_entry_count()) return false;
    usb_browser_entry_t entry;
    /* A directory that took the file's name is not the track we remembered. */
    if (!usb_storage_entry_at(index, &entry) ||
        entry.kind == USB_BROWSER_ENTRY_DIRECTORY) {
        return false;
    }
    player_usb_select_item(index);
    return true;
}

unsigned int player_control_usb_listing_revision(void)
{
    return atomic_load_explicit(&s_usb_listing_revision, memory_order_acquire);
}
