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

#include "audio_source.h"
#include "internet_radio.h"
#include "usb_browser.h"
#include "usb_player.h"
#include "usb_storage.h"
#include "wifi_provisioning.h"

#define PLAYER_COMMAND_QUEUE_LENGTH 8U
#define PLAYER_CONTROL_TASK_STACK_SIZE 6144U
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
    if (!usb_storage_entry_at(index, &entry)) {
        ESP_LOGW(TAG, "usb item %u is not in the current listing", (unsigned int)index);
        return;
    }
    char path[USB_BROWSER_PATH_MAX_LEN];
    char current[USB_BROWSER_PATH_MAX_LEN];
    if (!usb_storage_current_path(current, sizeof(current)) ||
        !usb_browser_path_child(current, entry.name, path, sizeof(path))) {
        ESP_LOGW(TAG, "cannot build a path for usb item %u", (unsigned int)index);
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
        return;
    }
    atomic_store_explicit(&s_usb_item_index, index, memory_order_release);
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
    player_refresh_rssi_if_due();
    snapshot->wifi_rssi_valid =
        atomic_load_explicit(&s_rssi_valid, memory_order_acquire);
    snapshot->wifi_rssi_dbm =
        (int8_t)atomic_load_explicit(&s_rssi_dbm, memory_order_relaxed);
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
