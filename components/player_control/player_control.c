#include "player_control.h"

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
#include "wifi_provisioning.h"

#define PLAYER_COMMAND_QUEUE_LENGTH 8U
#define PLAYER_CONTROL_TASK_STACK_SIZE 6144U
#define PLAYER_CONTROL_TASK_PRIORITY 5U

static const char *TAG = "player_control";

static QueueHandle_t s_command_queue;
static audio_source_manager_t s_manager;
static atomic_int s_active_source = ATOMIC_VAR_INIT(AUDIO_SOURCE_NONE);
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

static bool player_stop_active_source(audio_source_t source)
{
    if (source == AUDIO_SOURCE_INTERNET_RADIO) {
        const esp_err_t result = internet_radio_stop();
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "internet radio stop failed: %s", esp_err_to_name(result));
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
            }
            break;
        }
        case PLAYER_OPERATION_START_ITEM:
            (void)internet_radio_start_station_index(command.item_index);
            break;
        case PLAYER_OPERATION_START_SAVED:
            (void)internet_radio_start_saved_station();
            break;
        case PLAYER_OPERATION_PAUSE:
            (void)internet_radio_pause();
            break;
        case PLAYER_OPERATION_RESUME:
            (void)internet_radio_resume();
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
    snapshot->active_source =
        (audio_source_t)atomic_load_explicit(&s_active_source, memory_order_acquire);

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

const station_catalog_entry_t *player_control_station_at(size_t index)
{
    return internet_radio_station_at(index);
}
