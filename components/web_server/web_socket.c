#include "web_socket.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "web_json.h"
#include "web_view_model.h"

#ifdef ESP_PLATFORM
#include <stdatomic.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "player_control.h"
#include "station_catalog.h"
#include "yandex_catalog.h"
#endif

static void secure_zero(void *memory, size_t size)
{
    volatile unsigned char *bytes = memory;
    while (size-- > 0U) {
        *bytes++ = 0U;
    }
}

static int writer_finish(web_json_writer_t *writer)
{
    if (!web_json_valid(writer) || web_json_length(writer) > WEB_PROTOCOL_EVENT_MAX) {
        // Hand back an empty string rather than a truncated document: callers
        // treat a zero length as "do not send", and half a frame would parse
        // as garbage on the client.
        web_json_truncate(writer);
        return 0;
    }
    return (int)web_json_length(writer);
}

static bool request_id_valid(const char *request_id)
{
    if (request_id == NULL) {
        return false;
    }
    const size_t length = strlen(request_id);
    if (length == 0U || length > WEB_PROTOCOL_REQUEST_ID_MAX) {
        return false;
    }
    for (size_t index = 0U; index < length; ++index) {
        const unsigned char value = (unsigned char)request_id[index];
        if (!((value >= 'a' && value <= 'z') ||
              (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == '.' ||
              value == '_' || value == '-')) {
            return false;
        }
    }
    return true;
}

int web_server_command_result(char *output, size_t output_size,
                              const char *request_id, bool ok,
                              const char *error)
{
    web_json_writer_t writer;
    web_json_init(&writer, output, output_size, WEB_PROTOCOL_EVENT_MAX);
    if (!request_id_valid(request_id) || (!ok && error == NULL)) {
        web_json_invalidate(&writer);
        return writer_finish(&writer);
    }

    web_json_literal(&writer, "{\"type\":\"command.result\",\"id\":");
    web_json_string(&writer, request_id);
    web_json_literal(&writer, ok ? ",\"ok\":true" : ",\"ok\":false,\"error\":");
    if (!ok) {
        web_json_string(&writer, error);
    }
    web_json_literal(&writer, "}");
    return writer_finish(&writer);
}

static const char *wifi_mode_name(wifi_provisioning_mode_t mode)
{
    switch (mode) {
    case WIFI_PROVISIONING_AP_SETUP: return "ap_setup";
    case WIFI_PROVISIONING_STA_CONNECTING: return "sta_connecting";
    case WIFI_PROVISIONING_STA_CONNECTED: return "sta_connected";
    default: return "unknown";
    }
}

static void write_capabilities(web_json_writer_t *writer,
                               const player_snapshot_t *player)
{
    web_json_literal(writer, "\"capabilities\":[");
    bool written = false;
    if ((player->capabilities & PLAYER_CAP_INTERNET_RADIO) != 0U) {
        web_json_literal(writer,
                       "{\"id\":\"internet_radio\",\"label\":"
                       "\"Интернет-радио\",\"list_kind\":\"stations\"}");
        written = true;
    }
    /* A volume only appears while there is something to browse on it, so this
     * list is what tells the browser whether to offer the source at all. The
     * card is offered on the strength of the last look at the slot - it is not
     * held mounted, and mounting it to answer a status frame would take the
     * SRAM the radio is using. */
    if ((player->capabilities & PLAYER_CAP_USB) != 0U) {
        if (written) {
            web_json_literal(writer, ",");
        }
        web_json_literal(writer,
                       "{\"id\":\"usb\",\"label\":"
                       "\"USB-накопитель\",\"list_kind\":\"files\"}");
        written = true;
    }
    if ((player->capabilities & PLAYER_CAP_SD) != 0U) {
        if (written) {
            web_json_literal(writer, ",");
        }
        web_json_literal(writer,
                       "{\"id\":\"sd\",\"label\":"
                       "\"SD-карта\",\"list_kind\":\"files\"}");
        written = true;
    }
    /* Only while an account is linked, for the same reason the capability is:
     * without one the source can do nothing but fail. */
    if ((player->capabilities & PLAYER_CAP_YANDEX) != 0U) {
        if (written) {
            web_json_literal(writer, ",");
        }
        web_json_literal(writer,
                       "{\"id\":\"yandex\",\"label\":"
                       "\"ЯМузыка\",\"list_kind\":\"stations\"}");
    }
    web_json_literal(writer, "]");
}

static void write_player(web_json_writer_t *writer,
                         const player_snapshot_t *player)
{
    char artist[PLAYER_NAME_MAX_LEN];
    char title[PLAYER_TITLE_MAX_LEN];
    web_view_split_player_title(player->stream_title, player->context,
                                artist, sizeof(artist), title, sizeof(title));

    web_json_literal(writer, "\"player\":{\"state\":");
    web_json_string(writer, web_view_playback_name(player->playback_state));
    web_json_literal(writer, ",\"mode\":");
    web_json_string(writer, web_view_source_label(player->active_source));
    web_json_literal(writer, ",\"artist\":");
    web_json_string(writer, artist);
    web_json_literal(writer, ",\"title\":");
    web_json_string(writer, title);
    web_json_literal(writer, ",\"context\":");
    web_json_string(writer, player->context);
    web_json_literal(writer, ",\"codec\":");
    web_json_string(writer, player->codec);
    web_json_literal(writer, ",\"bitrate_kbps\":");
    web_json_format(writer, "%u", (unsigned)player->bitrate_kbps);
    web_json_literal(writer, ",\"sample_rate_hz\":");
    web_json_format(writer, "%u", (unsigned)player->sample_rate_hz);
    if (player->wifi_rssi_valid) {
        web_json_literal(writer, ",\"wifi_rssi_dbm\":");
        web_json_format(writer, "%d", (int)player->wifi_rssi_dbm);
    }
    /* Only for a source that has a library to be in, the way the signal is
     * only sent when there is one. A field always present would make the web
     * UI draw an empty heart on a radio station, offering a button that would
     * be refused. The frame is capped at 512 bytes and this is the section
     * that carries the titles, so it costs as few of them as it can. */
    if (player->track_likeable) {
        web_json_literal(writer, ",\"liked\":");
        web_json_literal(writer, player->track_liked ? "true" : "false");
    }
    web_json_literal(writer, ",\"error\":");
    web_json_string(writer, player->error);
    web_json_literal(writer, "}");

    secure_zero(artist, sizeof(artist));
    secure_zero(title, sizeof(title));
}

static void write_active_index(web_json_writer_t *writer,
                               const player_snapshot_t *player)
{
    if (player->active_item_index == PLAYER_ITEM_NONE ||
        player->active_item_index >= player->item_count) {
        web_json_literal(writer, "null");
    } else {
        web_json_format(writer, "%u", (unsigned)player->active_item_index);
    }
}

/* The USB listing is a header only: the entries themselves go over REST.
 * A directory may hold up to FILE_STORAGE_MAX_ENTRIES names of up to
 * FILE_BROWSER_NAME_MAX_LEN bytes each, which is far past the
 * WEB_PROTOCOL_EVENT_MAX frame this serializer writes into, and growing that
 * static buffer is the wrong trade in a firmware already short of internal
 * SRAM. The revision is what tells the browser to re-fetch: opening a sibling
 * directory can leave the path length, the count and the active index looking
 * identical. */
static void write_usb_list(web_json_writer_t *writer, const player_snapshot_t *player)
{
    web_json_literal(writer, "\"list\":{\"kind\":\"files\",\"active_index\":");
    write_active_index(writer, player);
    web_json_literal(writer, ",\"path\":");
    web_json_string(writer, player->context);
    web_json_literal(writer, ",\"revision\":");
    web_json_format(writer, "%u", player->listing_revision);
    web_json_literal(writer, ",\"count\":");
    web_json_format(writer, "%u", (unsigned)player->item_count);
    web_json_literal(writer, ",\"has_parent\":");
    web_json_literal(writer,
                   file_browser_path_is_root(player->context) ? "false" : "true");
    web_json_literal(writer, "}");
}

static void write_list(web_json_writer_t *writer, const player_snapshot_t *player,
                       web_socket_station_label_fn station_label,
                       void *station_context)
{
    if (audio_source_is_files(player->active_source)) {
        write_usb_list(writer, player);
        return;
    }
    web_json_literal(writer, "\"list\":{\"kind\":\"stations\",\"active_index\":");
    write_active_index(writer, player);
    web_json_literal(writer, ",\"items\":[");
    for (size_t index = 0U; index < player->item_count && web_json_valid(writer);
         ++index) {
        if (index > 0U) {
            web_json_literal(writer, ",");
        }
        web_json_literal(writer, "{\"index\":");
        web_json_format(writer, "%u", (unsigned)index);
        web_json_literal(writer, ",\"label\":");
        web_json_string_bounded(writer, station_label == NULL ? "" :
                                              station_label(index,
                                                            station_context),
                              48U);
        web_json_literal(writer, "}");
    }
    web_json_literal(writer, "]}");
}

static void write_wifi(web_json_writer_t *writer,
                       const web_socket_wifi_state_t *wifi)
{
    web_json_literal(writer, "\"wifi\":{\"mode\":");
    web_json_string(writer, wifi_mode_name(wifi->mode));
    web_json_literal(writer, ",\"active_ssid\":");
    web_json_string(writer, wifi->active_ssid);
    web_json_literal(writer, ",\"ip\":");
    web_json_string(writer, wifi->ipv4);
    web_json_literal(writer, ",\"save_pending\":");
    web_json_literal(writer, wifi->save_pending ? "true" : "false");
    web_json_literal(writer, ",\"last_error\":");
    web_json_format(writer, "%ld", (long)wifi->last_error);
    web_json_literal(writer, ",\"saved_ssids\":[");
    const uint8_t count = wifi->saved_count <= WIFI_SETTINGS_MAX_NETWORKS
                              ? wifi->saved_count
                              : WIFI_SETTINGS_MAX_NETWORKS;
    for (uint8_t index = 0U; index < count; ++index) {
        if (index > 0U) {
            web_json_literal(writer, ",");
        }
        web_json_string(writer, wifi->saved_ssids[index]);
    }
    web_json_literal(writer, "]}");
}

static const char *event_type(web_socket_event_kind_t kind)
{
    switch (kind) {
    case WEB_SOCKET_EVENT_SNAPSHOT: return "snapshot";
    case WEB_SOCKET_EVENT_CAPABILITIES_UPDATE: return "capabilities.update";
    case WEB_SOCKET_EVENT_PLAYER_UPDATE: return "player.update";
    case WEB_SOCKET_EVENT_LIST_UPDATE: return "list.update";
    case WEB_SOCKET_EVENT_WIFI_UPDATE: return "wifi.update";
    default: return NULL;
    }
}

int web_socket_serialize_event(char *output, size_t output_size,
                               web_socket_event_kind_t kind, uint32_t revision,
                               const player_snapshot_t *player,
                               const web_socket_wifi_state_t *wifi,
                               web_socket_station_label_fn station_label,
                               void *station_context)
{
    web_json_writer_t writer;
    web_json_init(&writer, output, output_size, WEB_PROTOCOL_EVENT_MAX);
    const char *type = event_type(kind);
    if (type == NULL || player == NULL || wifi == NULL) {
        web_json_invalidate(&writer);
        return writer_finish(&writer);
    }

    web_json_literal(&writer, "{\"type\":");
    web_json_string(&writer, type);
    web_json_literal(&writer, ",\"revision\":");
    web_json_format(&writer, "%u", (unsigned)revision);

    switch (kind) {
    case WEB_SOCKET_EVENT_SNAPSHOT:
        web_json_literal(&writer, ",");
        write_capabilities(&writer, player);
        web_json_literal(&writer, ",\"active_source\":");
        web_json_string(&writer, web_view_source_name(player->active_source));
        web_json_literal(&writer, ",");
        write_player(&writer, player);
        web_json_literal(&writer, ",");
        write_list(&writer, player, station_label, station_context);
        web_json_literal(&writer, ",");
        write_wifi(&writer, wifi);
        break;
    case WEB_SOCKET_EVENT_CAPABILITIES_UPDATE:
        web_json_literal(&writer, ",");
        write_capabilities(&writer, player);
        break;
    case WEB_SOCKET_EVENT_PLAYER_UPDATE:
        web_json_literal(&writer, ",\"active_source\":");
        web_json_string(&writer, web_view_source_name(player->active_source));
        web_json_literal(&writer, ",");
        write_player(&writer, player);
        break;
    case WEB_SOCKET_EVENT_LIST_UPDATE:
        web_json_literal(&writer, ",");
        write_list(&writer, player, station_label, station_context);
        break;
    case WEB_SOCKET_EVENT_WIFI_UPDATE:
        web_json_literal(&writer, ",");
        write_wifi(&writer, wifi);
        break;
    default:
        web_json_invalidate(&writer);
        break;
    }
    web_json_literal(&writer, "}");
    return writer_finish(&writer);
}

static bool wifi_state_equal(const web_socket_wifi_state_t *left,
                             const web_socket_wifi_state_t *right)
{
    if (left == right) {
        return true;
    }
    if (left == NULL || right == NULL || left->mode != right->mode ||
        left->last_error != right->last_error ||
        left->save_pending != right->save_pending ||
        left->saved_count != right->saved_count ||
        strcmp(left->active_ssid, right->active_ssid) != 0 ||
        strcmp(left->ipv4, right->ipv4) != 0) {
        return false;
    }
    const uint8_t count = left->saved_count <= WIFI_SETTINGS_MAX_NETWORKS
                              ? left->saved_count
                              : WIFI_SETTINGS_MAX_NETWORKS;
    for (uint8_t index = 0U; index < count; ++index) {
        if (strcmp(left->saved_ssids[index], right->saved_ssids[index]) != 0) {
            return false;
        }
    }
    return true;
}

uint32_t web_socket_changed_sections(
    const player_snapshot_t *previous_player,
    const web_socket_wifi_state_t *previous_wifi,
    const player_snapshot_t *current_player,
    const web_socket_wifi_state_t *current_wifi)
{
    if (previous_player == NULL || previous_wifi == NULL ||
        current_player == NULL || current_wifi == NULL) {
        return WEB_SOCKET_SECTION_ALL;
    }

    const uint32_t player_changes =
        web_view_snapshot_changes(previous_player, current_player);
    uint32_t changes = WEB_SOCKET_SECTION_NONE;
    if ((player_changes & WEB_VIEW_SECTION_CAPABILITIES) != 0U) {
        changes |= WEB_SOCKET_SECTION_CAPABILITIES;
    }
    if ((player_changes & WEB_VIEW_SECTION_PLAYER) != 0U) {
        changes |= WEB_SOCKET_SECTION_PLAYER;
    }
    if ((player_changes & WEB_VIEW_SECTION_LIST) != 0U) {
        changes |= WEB_SOCKET_SECTION_LIST;
    }
    if (!wifi_state_equal(previous_wifi, current_wifi)) {
        changes |= WEB_SOCKET_SECTION_WIFI;
    }
    return changes;
}

uint16_t web_socket_rejection_close_code(bool final, uint8_t frame_type,
                                          size_t payload_length)
{
    if (!final || frame_type == WEB_SOCKET_FRAME_BINARY ||
        frame_type == WEB_SOCKET_FRAME_CONTINUATION) {
        return 1002U;
    }
    if (frame_type == WEB_SOCKET_FRAME_TEXT) {
        return payload_length > WEB_PROTOCOL_FRAME_MAX ? 1009U : 0U;
    }
    if (frame_type == WEB_SOCKET_FRAME_PING ||
        frame_type == WEB_SOCKET_FRAME_PONG ||
        frame_type == WEB_SOCKET_FRAME_CLOSE) {
        return payload_length > 125U ? 1002U : 0U;
    }
    return 1002U;
}

uint32_t web_socket_committed_revision(uint32_t current, bool complete)
{
    if (!complete) {
        return current;
    }
    return current == UINT32_MAX ? 1U : current + 1U;
}

#ifdef ESP_PLATFORM
#define WEB_SOCKET_WIFI_QUEUE_LENGTH 4U
#define WEB_SOCKET_WIFI_TASK_STACK_SIZE 4096U
#define WEB_SOCKET_WIFI_TASK_PRIORITY 3U
#define WEB_SOCKET_BROADCAST_TASK_STACK_SIZE 4096U
#define WEB_SOCKET_BROADCAST_TASK_PRIORITY 3U
#define WEB_SOCKET_BROADCAST_PERIOD_MS 250U
#define WEB_SOCKET_SEND_JOB_COUNT (WEB_SOCKET_MAX_CLIENTS + 1U)
#define WEB_SOCKET_CLIENT_FD_CAPACITY WEB_SOCKET_SERVER_SOCKET_CAPACITY
#define WEB_SOCKET_CONTROL_FRAME_MAX 125U

typedef struct {
    bool in_use;
    wifi_network_t network;
} wifi_secret_slot_t;

typedef struct {
    bool in_use;
    bool initial_snapshot;
    int target_fd;
    uint32_t sections;
    player_snapshot_t player;
    web_socket_wifi_state_t wifi;
} web_socket_send_job_t;

static const char *TAG = "web_socket";
static httpd_handle_t s_server;
static QueueHandle_t s_wifi_queue;
static TaskHandle_t s_wifi_task;
static TaskHandle_t s_broadcast_task;
static atomic_bool s_running = ATOMIC_VAR_INIT(false);
static atomic_uint s_wifi_posters = ATOMIC_VAR_INIT(0U);
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static web_socket_send_job_t s_send_jobs[WEB_SOCKET_SEND_JOB_COUNT];
static wifi_secret_slot_t s_wifi_secrets[WEB_SOCKET_WIFI_QUEUE_LENGTH];
static bool s_wifi_command_busy;
static int s_ready_clients[WEB_SOCKET_MAX_CLIENTS];
static bool s_ready_client_used[WEB_SOCKET_MAX_CLIENTS];
static player_snapshot_t s_published_player;
static web_socket_wifi_state_t s_published_wifi;
static bool s_have_published;
static bool s_broadcast_pending;
/* All queued work executes serially in the HTTP server task. */
static char s_http_output[WEB_PROTOCOL_EVENT_MAX + 1U];
static uint32_t s_revision = 1U;

static void capture_wifi_state(web_socket_wifi_state_t *output)
{
    memset(output, 0, sizeof(*output));
    const wifi_provisioning_status_t status = wifi_provisioning_status();
    wifi_provisioning_saved_ssids_t saved = wifi_provisioning_committed_ssids();
    output->mode = status.mode;
    output->last_error = status.last_error;
    output->save_pending = status.save_pending;
    snprintf(output->active_ssid, sizeof(output->active_ssid), "%s",
             status.active_ssid);
    snprintf(output->ipv4, sizeof(output->ipv4), "%s", status.ipv4);
    output->saved_count = saved.count <= WIFI_SETTINGS_MAX_NETWORKS
                              ? saved.count
                              : WIFI_SETTINGS_MAX_NETWORKS;
    for (uint8_t index = 0U; index < output->saved_count; ++index) {
        snprintf(output->saved_ssids[index], sizeof(output->saved_ssids[index]),
                 "%s", saved.ssids[index]);
    }
    secure_zero(&saved, sizeof(saved));
}

static int wifi_secret_acquire(const wifi_network_t *network)
{
    int slot = -1;
    taskENTER_CRITICAL(&s_state_lock);
    if (!s_wifi_command_busy) {
        for (size_t index = 0U; index < WEB_SOCKET_WIFI_QUEUE_LENGTH; ++index) {
            if (!s_wifi_secrets[index].in_use) {
                s_wifi_secrets[index].in_use = true;
                s_wifi_secrets[index].network = *network;
                s_wifi_command_busy = true;
                slot = (int)index;
                break;
            }
        }
    }
    taskEXIT_CRITICAL(&s_state_lock);
    return slot;
}

static void wifi_command_finish(void)
{
    taskENTER_CRITICAL(&s_state_lock);
    s_wifi_command_busy = false;
    taskEXIT_CRITICAL(&s_state_lock);
}

static void wifi_secret_release(uint8_t slot)
{
    if (slot >= WEB_SOCKET_WIFI_QUEUE_LENGTH) {
        return;
    }
    taskENTER_CRITICAL(&s_state_lock);
    secure_zero(&s_wifi_secrets[slot], sizeof(s_wifi_secrets[slot]));
    taskEXIT_CRITICAL(&s_state_lock);
}

static void ready_client_remove(int fd)
{
    for (size_t index = 0U; index < WEB_SOCKET_MAX_CLIENTS; ++index) {
        if (s_ready_client_used[index] && s_ready_clients[index] == fd) {
            s_ready_client_used[index] = false;
            s_ready_clients[index] = -1;
        }
    }
}

static bool ready_client_add(int fd)
{
    ready_client_remove(fd);
    for (size_t index = 0U; index < WEB_SOCKET_MAX_CLIENTS; ++index) {
        if (!s_ready_client_used[index]) {
            s_ready_client_used[index] = true;
            s_ready_clients[index] = fd;
            return true;
        }
    }
    return false;
}

/* Which list the rows come from depends on the source: both are flat lists of
 * stations, but one is the catalog file and the other is fetched from the
 * account. The active source arrives through `context` rather than being read
 * again here, so the rows can never describe a different source than the
 * player fields serialized alongside them. */
static const char *runtime_station_label(size_t index, void *context)
{
    const audio_source_t source =
        context == NULL ? AUDIO_SOURCE_INTERNET_RADIO : *(const audio_source_t *)context;
    if (source == AUDIO_SOURCE_YANDEX) {
        static yandex_station_t station;
        return yandex_catalog_station_at(index, &station) ? station.name : "";
    }
    const station_catalog_entry_t *station = player_control_station_at(index);
    return station == NULL ? "" : station->name;
}

static web_socket_send_job_t *send_job_acquire(void)
{
    web_socket_send_job_t *job = NULL;
    taskENTER_CRITICAL(&s_state_lock);
    for (size_t index = 0U; index < WEB_SOCKET_SEND_JOB_COUNT; ++index) {
        if (!s_send_jobs[index].in_use) {
            memset(&s_send_jobs[index], 0, sizeof(s_send_jobs[index]));
            s_send_jobs[index].in_use = true;
            job = &s_send_jobs[index];
            break;
        }
    }
    taskEXIT_CRITICAL(&s_state_lock);
    return job;
}

static void send_job_release(web_socket_send_job_t *job)
{
    if (job == NULL) {
        return;
    }
    taskENTER_CRITICAL(&s_state_lock);
    secure_zero(job, sizeof(*job));
    taskEXIT_CRITICAL(&s_state_lock);
}

static void set_task_handle(TaskHandle_t *storage, TaskHandle_t value)
{
    taskENTER_CRITICAL(&s_state_lock);
    *storage = value;
    taskEXIT_CRITICAL(&s_state_lock);
}

static TaskHandle_t get_task_handle(TaskHandle_t *storage)
{
    taskENTER_CRITICAL(&s_state_lock);
    const TaskHandle_t value = *storage;
    taskEXIT_CRITICAL(&s_state_lock);
    return value;
}

static const char *runtime_event_type(web_socket_event_kind_t kind)
{
    const char *type = event_type(kind);
    return type == NULL ? "unknown" : type;
}

static bool prepare_event_frame(httpd_ws_frame_t *frame,
                                web_socket_event_kind_t kind,
                                uint32_t revision,
                                const player_snapshot_t *player,
                                const web_socket_wifi_state_t *wifi)
{
    const audio_source_t list_source = player->active_source;
    const int length = web_socket_serialize_event(
        s_http_output, sizeof(s_http_output), kind, revision, player, wifi,
        runtime_station_label, (void *)&list_source);
    if (length <= 0) {
        ESP_LOGE(TAG, "serialize event failed type=%s max=%u",
                 runtime_event_type(kind), (unsigned)WEB_PROTOCOL_EVENT_MAX);
        return false;
    }

    *frame = (httpd_ws_frame_t){
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)s_http_output,
        .len = (size_t)length,
    };
    return true;
}

static void close_failed_client(int fd)
{
    ready_client_remove(fd);
    (void)httpd_sess_trigger_close(s_server, fd);
}

static bool send_prepared_frame(int fd, web_socket_event_kind_t kind,
                                httpd_ws_frame_t *frame)
{
    const esp_err_t err = httpd_ws_send_frame_async(s_server, fd, frame);
    if (err == ESP_OK) {
        return true;
    }
    ESP_LOGW(TAG, "send event failed type=%s size=%u err=%s; closing fd=%d",
             runtime_event_type(kind), (unsigned)frame->len,
             esp_err_to_name(err), fd);
    close_failed_client(fd);
    return false;
}

static void send_prepared_to_ready(web_socket_event_kind_t kind,
                                   httpd_ws_frame_t *frame)
{
    for (size_t index = 0U; index < WEB_SOCKET_MAX_CLIENTS; ++index) {
        if (!s_ready_client_used[index]) {
            continue;
        }
        const int fd = s_ready_clients[index];
        if (httpd_ws_get_fd_info(s_server, fd) !=
            HTTPD_WS_CLIENT_WEBSOCKET) {
            ready_client_remove(fd);
            continue;
        }
        (void)send_prepared_frame(fd, kind, frame);
    }
}

static void finish_broadcast_job(const web_socket_send_job_t *job,
                                 bool complete)
{
    taskENTER_CRITICAL(&s_state_lock);
    if (complete) {
        s_published_player = job->player;
        s_published_wifi = job->wifi;
        s_have_published = true;
    }
    s_broadcast_pending = false;
    taskEXIT_CRITICAL(&s_state_lock);
}

static void send_job_work(void *context)
{
    web_socket_send_job_t *job = context;
    if (job == NULL || !job->in_use || s_server == NULL) {
        if (job != NULL && job->sections != WEB_SOCKET_SECTION_NONE) {
            finish_broadcast_job(job, false);
        }
        send_job_release(job);
        return;
    }

    if (job->initial_snapshot) {
        player_snapshot_t player;
        web_socket_wifi_state_t wifi;
        player_control_get_snapshot(&player);
        capture_wifi_state(&wifi);
        httpd_ws_frame_t frame;
        const bool prepared = prepare_event_frame(
            &frame, WEB_SOCKET_EVENT_SNAPSHOT, s_revision, &player, &wifi);
        const bool websocket = httpd_ws_get_fd_info(s_server, job->target_fd) ==
                               HTTPD_WS_CLIENT_WEBSOCKET;
        if (!prepared || !websocket ||
            !send_prepared_frame(job->target_fd, WEB_SOCKET_EVENT_SNAPSHOT,
                                 &frame) ||
            !ready_client_add(job->target_fd)) {
            close_failed_client(job->target_fd);
        }
        secure_zero(&player, sizeof(player));
        secure_zero(&wifi, sizeof(wifi));
    } else {
        static const struct {
            uint32_t section;
            web_socket_event_kind_t kind;
        } events[] = {
            {WEB_SOCKET_SECTION_CAPABILITIES,
             WEB_SOCKET_EVENT_CAPABILITIES_UPDATE},
            {WEB_SOCKET_SECTION_PLAYER, WEB_SOCKET_EVENT_PLAYER_UPDATE},
            {WEB_SOCKET_SECTION_LIST, WEB_SOCKET_EVENT_LIST_UPDATE},
            {WEB_SOCKET_SECTION_WIFI, WEB_SOCKET_EVENT_WIFI_UPDATE},
        };
        bool complete = true;
        for (size_t index = 0U; index < sizeof(events) / sizeof(events[0]);
             ++index) {
            if ((job->sections & events[index].section) == 0U) {
                continue;
            }
            const uint32_t candidate =
                web_socket_committed_revision(s_revision, true);
            httpd_ws_frame_t frame;
            if (!prepare_event_frame(&frame, events[index].kind, candidate,
                                     &job->player, &job->wifi)) {
                complete = false;
                break;
            }
            s_revision = candidate;
            send_prepared_to_ready(events[index].kind, &frame);
        }
        finish_broadcast_job(job, complete);
    }
    secure_zero(s_http_output, sizeof(s_http_output));
    send_job_release(job);
}

static bool queue_send_job(web_socket_send_job_t *job)
{
    if (job == NULL || s_server == NULL ||
        httpd_queue_work(s_server, send_job_work, job) != ESP_OK) {
        if (job != NULL && job->sections != WEB_SOCKET_SECTION_NONE) {
            finish_broadcast_job(job, false);
        }
        send_job_release(job);
        return false;
    }
    return true;
}

static bool queue_initial_snapshot(int fd)
{
    web_socket_send_job_t *job = send_job_acquire();
    if (job == NULL) {
        return false;
    }
    job->initial_snapshot = true;
    job->target_fd = fd;
    return queue_send_job(job);
}

static void wifi_worker_task(void *context)
{
    (void)context;
    uint8_t slot = 0U;
    while (atomic_load_explicit(&s_running, memory_order_acquire)) {
        if (xQueueReceive(s_wifi_queue, &slot, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }
        if (slot >= WEB_SOCKET_WIFI_QUEUE_LENGTH ||
            !s_wifi_secrets[slot].in_use) {
            wifi_command_finish();
            continue;
        }
        const esp_err_t err = wifi_provisioning_save_network(
            s_wifi_secrets[slot].network.ssid,
            s_wifi_secrets[slot].network.password);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Wi-Fi settings apply failed err=%s",
                     esp_err_to_name(err));
        }
        wifi_secret_release(slot);
        if (err == ESP_OK) {
            while (atomic_load_explicit(&s_running, memory_order_acquire) &&
                   wifi_provisioning_status().save_pending) {
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
        wifi_command_finish();
        slot = 0U;
    }
    slot = 0U;
    set_task_handle(&s_wifi_task, NULL);
    vTaskDelete(NULL);
}

static void broadcaster_task(void *context)
{
    (void)context;
    TickType_t last_wake = xTaskGetTickCount();

    while (atomic_load_explicit(&s_running, memory_order_acquire)) {
        player_snapshot_t player;
        web_socket_wifi_state_t wifi;
        player_control_get_snapshot(&player);
        capture_wifi_state(&wifi);
        player_snapshot_t published_player;
        web_socket_wifi_state_t published_wifi;
        bool have_published;
        bool pending;
        taskENTER_CRITICAL(&s_state_lock);
        have_published = s_have_published;
        pending = s_broadcast_pending;
        published_player = s_published_player;
        published_wifi = s_published_wifi;
        if (!have_published) {
            s_published_player = player;
            s_published_wifi = wifi;
            s_have_published = true;
        }
        taskEXIT_CRITICAL(&s_state_lock);

        if (have_published && !pending) {
            const uint32_t sections = web_socket_changed_sections(
                &published_player, &published_wifi, &player, &wifi);
            if (sections != WEB_SOCKET_SECTION_NONE) {
                web_socket_send_job_t *job = send_job_acquire();
                if (job != NULL) {
                    job->sections = sections;
                    job->player = player;
                    job->wifi = wifi;
                    taskENTER_CRITICAL(&s_state_lock);
                    if (!s_broadcast_pending) {
                        s_broadcast_pending = true;
                    } else {
                        job->sections = WEB_SOCKET_SECTION_NONE;
                    }
                    taskEXIT_CRITICAL(&s_state_lock);
                    if (job->sections == WEB_SOCKET_SECTION_NONE) {
                        send_job_release(job);
                    } else {
                        (void)queue_send_job(job);
                    }
                }
            }
        }
        secure_zero(&published_player, sizeof(published_player));
        secure_zero(&published_wifi, sizeof(published_wifi));
        vTaskDelayUntil(&last_wake,
                        pdMS_TO_TICKS(WEB_SOCKET_BROADCAST_PERIOD_MS));
    }

    set_task_handle(&s_broadcast_task, NULL);
    vTaskDelete(NULL);
}

bool web_socket_queue_wifi(const wifi_network_t *network)
{
    if (network == NULL ||
        !atomic_load_explicit(&s_running, memory_order_acquire)) {
        return false;
    }
    atomic_fetch_add_explicit(&s_wifi_posters, 1U, memory_order_acq_rel);
    if (!atomic_load_explicit(&s_running, memory_order_acquire) ||
        s_wifi_queue == NULL) {
        atomic_fetch_sub_explicit(&s_wifi_posters, 1U, memory_order_acq_rel);
        return false;
    }
    const int slot = wifi_secret_acquire(network);
    bool queued = false;
    if (slot >= 0) {
        const uint8_t slot_index = (uint8_t)slot;
        queued = xQueueSend(s_wifi_queue, &slot_index, 0) == pdTRUE;
        if (!queued) {
            wifi_secret_release(slot_index);
            wifi_command_finish();
        }
    }
    atomic_fetch_sub_explicit(&s_wifi_posters, 1U, memory_order_acq_rel);
    return queued;
}

static esp_err_t send_command_result(httpd_req_t *request,
                                     const char *request_id, bool ok,
                                     const char *error)
{
    char output[256];
    const int length = web_server_command_result(
        output, sizeof(output), request_id, ok, error);
    if (length <= 0) {
        return ESP_FAIL;
    }
    httpd_ws_frame_t frame = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)output,
        .len = (size_t)length,
    };
    const esp_err_t err = httpd_ws_send_frame(request, &frame);
    secure_zero(output, sizeof(output));
    if (err != ESP_OK) {
        close_failed_client(httpd_req_to_sockfd(request));
    }
    return err;
}

static esp_err_t reject_websocket_frame(httpd_req_t *request,
                                        uint16_t close_code)
{
    uint8_t payload[2] = {
        (uint8_t)(close_code >> 8U),
        (uint8_t)(close_code & 0xFFU),
    };
    httpd_ws_frame_t close_frame = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_CLOSE,
        .payload = payload,
        .len = sizeof(payload),
    };
    const int fd = httpd_req_to_sockfd(request);
    const esp_err_t result = httpd_ws_send_frame(request, &close_frame);
    secure_zero(payload, sizeof(payload));
    ready_client_remove(fd);
    (void)httpd_sess_trigger_close(request->handle, fd);
    return result;
}

static esp_err_t handle_text_frame(httpd_req_t *request, uint8_t *payload,
                                   size_t length)
{
    web_command_t command;
    const web_protocol_result_t parsed = web_protocol_parse_command(
        (const char *)payload, length, &command);
    bool accepted = false;
    const char *request_id = "invalid";
    const char *error = "Некорректная команда";
    if (parsed == WEB_PROTOCOL_OK) {
        request_id = command.request_id;
        if (command.kind == WEB_COMMAND_PLAYER) {
            accepted = player_control_post(&command.player);
        } else if (command.kind == WEB_COMMAND_WIFI_SAVE) {
            accepted = web_socket_queue_wifi(&command.wifi);
        }
        error = accepted ? "" : "Устройство занято";
    }

    const esp_err_t result = send_command_result(request, request_id, accepted,
                                                  error);
    web_protocol_clear_command(&command);
    return result;
}

static size_t websocket_client_count(httpd_handle_t server)
{
    int client_fds[WEB_SOCKET_CLIENT_FD_CAPACITY];
    size_t client_count = WEB_SOCKET_CLIENT_FD_CAPACITY;
    if (httpd_get_client_list(server, &client_count, client_fds) != ESP_OK) {
        return WEB_SOCKET_MAX_CLIENTS + 1U;
    }
    size_t websocket_count = 0U;
    for (size_t index = 0U; index < client_count; ++index) {
        if (httpd_ws_get_fd_info(server, client_fds[index]) ==
            HTTPD_WS_CLIENT_WEBSOCKET) {
            ++websocket_count;
        }
    }
    return websocket_count;
}

static esp_err_t websocket_handler(httpd_req_t *request)
{
    if (request->method == HTTP_GET) {
        /* The current connection is already marked as WebSocket here. */
        if (websocket_client_count(request->handle) > WEB_SOCKET_MAX_CLIENTS) {
            ESP_LOGW(TAG, "WebSocket client limit reached");
            return ESP_FAIL;
        }
        const int fd = httpd_req_to_sockfd(request);
        ready_client_remove(fd);
        if (!queue_initial_snapshot(fd)) {
            ESP_LOGW(TAG, "initial snapshot queue is busy");
            (void)httpd_sess_trigger_close(request->handle, fd);
            return ESP_FAIL;
        }
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {0};
    esp_err_t err = httpd_ws_recv_frame(request, &frame, 0U);
    if (err != ESP_OK) {
        close_failed_client(httpd_req_to_sockfd(request));
        return err;
    }

    const uint16_t rejection_code = web_socket_rejection_close_code(
        frame.final, (uint8_t)frame.type, frame.len);
    if (rejection_code != 0U) {
        return reject_websocket_frame(request, rejection_code);
    }

    uint8_t payload[WEB_PROTOCOL_FRAME_MAX + 1U] = {0};
    if (frame.len > 0U) {
        frame.payload = payload;
        err = httpd_ws_recv_frame(request, &frame,
                                  frame.type == HTTPD_WS_TYPE_TEXT
                                      ? WEB_PROTOCOL_FRAME_MAX
                                      : WEB_SOCKET_CONTROL_FRAME_MAX);
        if (err != ESP_OK) {
            secure_zero(payload, sizeof(payload));
            close_failed_client(httpd_req_to_sockfd(request));
            return err;
        }
    }

    if (frame.type == HTTPD_WS_TYPE_TEXT) {
        payload[frame.len] = '\0';
        err = handle_text_frame(request, payload, frame.len);
    } else if (frame.type == HTTPD_WS_TYPE_PING) {
        frame.type = HTTPD_WS_TYPE_PONG;
        err = httpd_ws_send_frame(request, &frame);
        if (err != ESP_OK) {
            close_failed_client(httpd_req_to_sockfd(request));
        }
    } else if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        frame.type = HTTPD_WS_TYPE_CLOSE;
        frame.payload = NULL;
        frame.len = 0U;
        err = httpd_ws_send_frame(request, &frame);
        ready_client_remove(httpd_req_to_sockfd(request));
        (void)httpd_sess_trigger_close(request->handle,
                                       httpd_req_to_sockfd(request));
    } else {
        err = ESP_OK;
    }
    secure_zero(payload, sizeof(payload));
    return err;
}

static bool tasks_stopped(void)
{
    return get_task_handle(&s_wifi_task) == NULL &&
           get_task_handle(&s_broadcast_task) == NULL &&
           atomic_load_explicit(&s_wifi_posters, memory_order_acquire) == 0U;
}

static bool send_jobs_idle(void)
{
    bool idle = true;
    taskENTER_CRITICAL(&s_state_lock);
    for (size_t index = 0U; index < WEB_SOCKET_SEND_JOB_COUNT; ++index) {
        if (s_send_jobs[index].in_use) {
            idle = false;
            break;
        }
    }
    taskEXIT_CRITICAL(&s_state_lock);
    return idle;
}

static void wipe_wifi_queue(void)
{
    if (s_wifi_queue == NULL) {
        return;
    }
    uint8_t slot = 0U;
    while (xQueueReceive(s_wifi_queue, &slot, 0) == pdTRUE) {
        wifi_secret_release(slot);
    }
    secure_zero(s_wifi_secrets, sizeof(s_wifi_secrets));
    wifi_command_finish();
}

static esp_err_t wait_for_shutdown(uint32_t timeout_ms)
{
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    while ((!tasks_stopped() || !send_jobs_idle()) &&
           (int32_t)(deadline - xTaskGetTickCount()) > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return tasks_stopped() && send_jobs_idle() ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void close_websocket_clients(httpd_handle_t server)
{
    int client_fds[WEB_SOCKET_CLIENT_FD_CAPACITY];
    size_t client_count = WEB_SOCKET_CLIENT_FD_CAPACITY;
    if (httpd_get_client_list(server, &client_count, client_fds) != ESP_OK) {
        return;
    }
    for (size_t index = 0U; index < client_count; ++index) {
        if (httpd_ws_get_fd_info(server, client_fds[index]) ==
            HTTPD_WS_CLIENT_WEBSOCKET) {
            (void)httpd_sess_trigger_close(server, client_fds[index]);
        }
    }
}

static esp_err_t wait_for_clients_closed(httpd_handle_t server,
                                         uint32_t timeout_ms)
{
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    while (websocket_client_count(server) != 0U &&
           (int32_t)(deadline - xTaskGetTickCount()) > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return websocket_client_count(server) == 0U ? ESP_OK : ESP_ERR_TIMEOUT;
}

static esp_err_t cleanup_failed_start(esp_err_t start_error)
{
    atomic_store_explicit(&s_running, false, memory_order_release);
    const esp_err_t shutdown_result = wait_for_shutdown(500U);
    if (shutdown_result != ESP_OK) {
        /* Preserve queue/server state while a task may still reference it. */
        return shutdown_result;
    }
    wipe_wifi_queue();
    vQueueDelete(s_wifi_queue);
    s_wifi_queue = NULL;
    s_server = NULL;
    secure_zero(s_send_jobs, sizeof(s_send_jobs));
    secure_zero(s_ready_clients, sizeof(s_ready_clients));
    secure_zero(s_ready_client_used, sizeof(s_ready_client_used));
    secure_zero(&s_published_player, sizeof(s_published_player));
    secure_zero(&s_published_wifi, sizeof(s_published_wifi));
    s_have_published = false;
    s_broadcast_pending = false;
    secure_zero(s_http_output, sizeof(s_http_output));
    return start_error;
}

esp_err_t web_socket_start(httpd_handle_t server)
{
    if (server == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_server != NULL) {
        return s_server == server &&
                       atomic_load_explicit(&s_running,
                                            memory_order_acquire)
                   ? ESP_OK
                   : ESP_ERR_INVALID_STATE;
    }

    memset(s_send_jobs, 0, sizeof(s_send_jobs));
    secure_zero(s_wifi_secrets, sizeof(s_wifi_secrets));
    memset(s_ready_client_used, 0, sizeof(s_ready_client_used));
    for (size_t index = 0U; index < WEB_SOCKET_MAX_CLIENTS; ++index) {
        s_ready_clients[index] = -1;
    }
    secure_zero(&s_published_player, sizeof(s_published_player));
    secure_zero(&s_published_wifi, sizeof(s_published_wifi));
    s_have_published = false;
    s_broadcast_pending = false;
    secure_zero(s_http_output, sizeof(s_http_output));
    atomic_store_explicit(&s_wifi_posters, 0U, memory_order_release);
    set_task_handle(&s_wifi_task, NULL);
    set_task_handle(&s_broadcast_task, NULL);
    s_revision = 1U;
    s_server = server;
    s_wifi_queue = xQueueCreate(WEB_SOCKET_WIFI_QUEUE_LENGTH,
                                sizeof(uint8_t));
    if (s_wifi_queue == NULL) {
        s_server = NULL;
        return ESP_ERR_NO_MEM;
    }

    atomic_store_explicit(&s_running, true, memory_order_release);
    if (xTaskCreate(wifi_worker_task, "web_wifi",
                    WEB_SOCKET_WIFI_TASK_STACK_SIZE, NULL,
                    WEB_SOCKET_WIFI_TASK_PRIORITY, &s_wifi_task) != pdPASS) {
        return cleanup_failed_start(ESP_ERR_NO_MEM);
    }
    if (xTaskCreate(broadcaster_task, "web_broadcast",
                    WEB_SOCKET_BROADCAST_TASK_STACK_SIZE, NULL,
                    WEB_SOCKET_BROADCAST_TASK_PRIORITY,
                    &s_broadcast_task) != pdPASS) {
        return cleanup_failed_start(ESP_ERR_NO_MEM);
    }

    const httpd_uri_t websocket_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = websocket_handler,
        .user_ctx = NULL,
        .is_websocket = true,
        .handle_ws_control_frames = true,
        .supported_subprotocol = NULL,
    };
    const esp_err_t err = httpd_register_uri_handler(server, &websocket_uri);
    if (err != ESP_OK) {
        return cleanup_failed_start(err);
    }

    ESP_LOGI(TAG, "WebSocket endpoint ready; max_clients=%u frame_max=%u",
             (unsigned)WEB_SOCKET_MAX_CLIENTS,
             (unsigned)WEB_PROTOCOL_FRAME_MAX);
    return ESP_OK;
}

esp_err_t web_socket_stop(void)
{
    if (s_server == NULL) {
        return ESP_OK;
    }
    const httpd_handle_t server = s_server;
    const esp_err_t unregister_result =
        httpd_unregister_uri_handler(server, "/ws", HTTP_GET);
    atomic_store_explicit(&s_running, false, memory_order_release);
    close_websocket_clients(server);
    const esp_err_t shutdown_result = wait_for_shutdown(6000U);
    if (shutdown_result != ESP_OK) {
        return shutdown_result;
    }
    const esp_err_t clients_result = wait_for_clients_closed(server, 1000U);
    if (clients_result != ESP_OK) {
        return clients_result;
    }

    wipe_wifi_queue();
    vQueueDelete(s_wifi_queue);
    s_wifi_queue = NULL;
    s_server = NULL;
    secure_zero(s_send_jobs, sizeof(s_send_jobs));
    secure_zero(s_ready_clients, sizeof(s_ready_clients));
    secure_zero(s_ready_client_used, sizeof(s_ready_client_used));
    secure_zero(&s_published_player, sizeof(s_published_player));
    secure_zero(&s_published_wifi, sizeof(s_published_wifi));
    s_have_published = false;
    s_broadcast_pending = false;
    secure_zero(s_http_output, sizeof(s_http_output));
    return unregister_result == ESP_ERR_NOT_FOUND ? ESP_OK : unregister_result;
}
#endif
