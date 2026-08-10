#include "web_server.h"
#include "web_socket.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static void web_server_secure_zero(void *memory, size_t size)
{
    volatile unsigned char *bytes = memory;
    while (size-- > 0U) {
        *bytes++ = 0U;
    }
}

#ifdef ESP_PLATFORM
#include <stdio.h>
#include <stdlib.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_http_server.h"

#include "wifi_provisioning.h"
#include "internet_radio.h"
#include "player_control.h"

#define WEB_SERVER_MOUNT_PATH "/littlefs"
#define WEB_SERVER_WEB_ROOT "www"
#define WEB_SERVER_REQUEST_MAX_LEN 256
#define WEB_SERVER_PLAYLIST_MAX_LEN 16384

static const char *TAG = "web";
static httpd_handle_t s_server;
static bool s_websocket_started;
static bool s_websocket_recovery_required;
#endif

static void json_skip_whitespace(const char **cursor)
{
    while (**cursor == ' ' || **cursor == '\t' || **cursor == '\r' || **cursor == '\n') {
        ++*cursor;
    }
}

static bool json_parse_string(const char **cursor, char *output, size_t output_size)
{
    if (**cursor != '"' || output_size == 0) {
        return false;
    }
    ++*cursor;
    size_t length = 0;
    while (**cursor != '\0' && **cursor != '"') {
        char value = **cursor;
        ++*cursor;
        if (value == '\\') {
            switch (**cursor) {
            case '"':
            case '\\':
            case '/':
                value = **cursor;
                break;
            case 'b':
                value = '\b';
                break;
            case 'f':
                value = '\f';
                break;
            case 'n':
                value = '\n';
                break;
            case 'r':
                value = '\r';
                break;
            case 't':
                value = '\t';
                break;
            default:
                return false;
            }
            ++*cursor;
        } else if ((unsigned char)value < 0x20) {
            return false;
        }
        if (length + 1 >= output_size) {
            return false;
        }
        output[length++] = value;
    }
    if (**cursor != '"') {
        return false;
    }
    ++*cursor;
    output[length] = '\0';
    return true;
}

web_server_parse_result_t web_server_parse_wifi_request(const char *request, wifi_network_t *network)
{
    if (request == NULL || network == NULL) {
        return WEB_SERVER_PARSE_INVALID;
    }
    *network = (wifi_network_t){0};
    const char *cursor = request;
    bool seen_ssid = false;
    bool seen_password = false;
    json_skip_whitespace(&cursor);
    if (*cursor++ != '{') {
        return WEB_SERVER_PARSE_INVALID;
    }
    json_skip_whitespace(&cursor);
    while (*cursor != '}') {
        char key[10] = {0};
        if (!json_parse_string(&cursor, key, sizeof(key))) {
            return WEB_SERVER_PARSE_INVALID;
        }
        json_skip_whitespace(&cursor);
        if (*cursor++ != ':') {
            return WEB_SERVER_PARSE_INVALID;
        }
        json_skip_whitespace(&cursor);
        if (strcmp(key, "ssid") == 0 && !seen_ssid) {
            if (!json_parse_string(&cursor, network->ssid, sizeof(network->ssid))) {
                return WEB_SERVER_PARSE_INVALID;
            }
            seen_ssid = true;
        } else if (strcmp(key, "password") == 0 && !seen_password) {
            if (!json_parse_string(&cursor, network->password, sizeof(network->password))) {
                return WEB_SERVER_PARSE_INVALID;
            }
            seen_password = true;
        } else {
            return WEB_SERVER_PARSE_INVALID;
        }
        json_skip_whitespace(&cursor);
        if (*cursor == ',') {
            ++cursor;
            json_skip_whitespace(&cursor);
            if (*cursor == '}') {
                return WEB_SERVER_PARSE_INVALID;
            }
        } else if (*cursor != '}') {
            return WEB_SERVER_PARSE_INVALID;
        }
    }
    ++cursor;
    json_skip_whitespace(&cursor);
    if (*cursor != '\0' || !seen_ssid || !seen_password) {
        return WEB_SERVER_PARSE_INVALID;
    }

    wifi_settings_t settings = {0};
    const web_server_parse_result_t result =
        wifi_settings_upsert(&settings, network->ssid, network->password) ==
                WIFI_SETTINGS_OK
            ? WEB_SERVER_PARSE_OK
            : WEB_SERVER_PARSE_INVALID;
    web_server_secure_zero(&settings, sizeof(settings));
    return result;
}

#ifdef ESP_PLATFORM
// esp_http_server (see osal.h: a single httpd_os_thread_create call) runs
// exactly one worker task that services all connections sequentially, so
// this buffer is never touched by two handlers at once. Sharing it here
// avoids growing every handler's stack frame while still cutting chunk
// count/syscalls ~8x versus the previous 512-byte on-stack buffers.
#define WEB_SERVER_FILE_CHUNK_SIZE 4096
static char s_file_chunk_buffer[WEB_SERVER_FILE_CHUNK_SIZE];

static bool web_server_client_accepts_gzip(httpd_req_t *request)
{
    char header[128];
    if (httpd_req_get_hdr_value_str(request, "Accept-Encoding", header, sizeof(header)) !=
        ESP_OK) {
        return false;
    }
    return strstr(header, "gzip") != NULL;
}

static esp_err_t web_server_send_file(httpd_req_t *request, const char *relative_path,
                                      const char *content_type)
{
    char path[88];
    FILE *file = NULL;
    bool gzipped = false;
    if (web_server_client_accepts_gzip(request)) {
        snprintf(path, sizeof(path), WEB_SERVER_MOUNT_PATH "/%s.gz", relative_path);
        file = fopen(path, "r");
        gzipped = file != NULL;
    }
    if (file == NULL) {
        snprintf(path, sizeof(path), WEB_SERVER_MOUNT_PATH "/%s", relative_path);
        file = fopen(path, "r");
    }
    if (file == NULL) {
        httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "Web file not installed");
        return ESP_FAIL;
    }
    httpd_resp_set_type(request, content_type);
    if (gzipped) {
        httpd_resp_set_hdr(request, "Content-Encoding", "gzip");
    }
    // These assets only change when the web UI is re-flashed via
    // `idf.py littlefs-flash`, so caching them for an hour avoids
    // re-downloading them on every normal page load/navigation.
    httpd_resp_set_hdr(request, "Cache-Control", "public, max-age=3600");
    size_t bytes_read;
    while ((bytes_read = fread(s_file_chunk_buffer, 1, sizeof(s_file_chunk_buffer), file)) > 0) {
        const esp_err_t err = httpd_resp_send_chunk(request, s_file_chunk_buffer, bytes_read);
        if (err != ESP_OK) {
            fclose(file);
            return err;
        }
    }
    if (ferror(file)) {
        fclose(file);
        ESP_LOGE(TAG, "read web asset failed: %s", path);
        return ESP_FAIL;
    }
    fclose(file);
    return httpd_resp_send_chunk(request, NULL, 0);
}

static esp_err_t web_server_root_get(httpd_req_t *request)
{
    return web_server_send_file(request, WEB_SERVER_WEB_ROOT "/index.html",
                                "text/html; charset=utf-8");
}

static esp_err_t web_server_app_js_get(httpd_req_t *request)
{
    return web_server_send_file(request, WEB_SERVER_WEB_ROOT "/app.js",
                                "application/javascript; charset=utf-8");
}

static esp_err_t web_server_style_get(httpd_req_t *request)
{
    return web_server_send_file(request, WEB_SERVER_WEB_ROOT "/style.css",
                                "text/css; charset=utf-8");
}

static esp_err_t web_server_settings_get(httpd_req_t *request)
{
    return web_server_send_file(request, WEB_SERVER_WEB_ROOT "/settings.html",
                                "text/html; charset=utf-8");
}

static esp_err_t web_server_settings_js_get(httpd_req_t *request)
{
    return web_server_send_file(request, WEB_SERVER_WEB_ROOT "/settings.js",
                                "application/javascript; charset=utf-8");
}

static esp_err_t web_server_playlist_page_get(httpd_req_t *request)
{
    return web_server_send_file(request, WEB_SERVER_WEB_ROOT "/playlist.html",
                                "text/html; charset=utf-8");
}

static esp_err_t web_server_playlist_js_get(httpd_req_t *request)
{
    return web_server_send_file(request, WEB_SERVER_WEB_ROOT "/playlist.js",
                                "application/javascript; charset=utf-8");
}

static const char *web_server_radio_state_name(internet_radio_state_t state)
{
    switch (state) {
    case INTERNET_RADIO_STATE_STOPPED: return "stopped";
    case INTERNET_RADIO_STATE_CONNECTING: return "connecting";
    case INTERNET_RADIO_STATE_PLAYING: return "playing";
    case INTERNET_RADIO_STATE_PAUSED: return "paused";
    case INTERNET_RADIO_STATE_RECONNECTING: return "reconnecting";
    case INTERNET_RADIO_STATE_ERROR: return "error";
    default: return "unknown";
    }
}

static const char *web_server_mode_name(wifi_provisioning_mode_t mode)
{
    switch (mode) {
    case WIFI_PROVISIONING_AP_SETUP:
        return "ap_setup";
    case WIFI_PROVISIONING_STA_CONNECTING:
        return "sta_connecting";
    case WIFI_PROVISIONING_STA_CONNECTED:
        return "sta_connected";
    default:
        return "unknown";
    }
}

static esp_err_t web_server_status_get(httpd_req_t *request)
{
    const wifi_provisioning_status_t status = wifi_provisioning_status();
    wifi_provisioning_saved_ssids_t settings =
        wifi_provisioning_committed_ssids();
    cJSON *root = cJSON_CreateObject();
    cJSON *networks = root == NULL ? NULL : cJSON_AddArrayToObject(root, "saved_ssids");
    if (root == NULL || networks == NULL) {
        web_server_secure_zero(&settings, sizeof(settings));
        cJSON_Delete(root);
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    cJSON_AddStringToObject(root, "mode", web_server_mode_name(status.mode));
    cJSON_AddStringToObject(root, "active_ssid", status.active_ssid);
    cJSON_AddStringToObject(root, "ip", status.ipv4);
    cJSON_AddNumberToObject(root, "last_error", status.last_error);
    cJSON_AddBoolToObject(root, "save_pending", status.save_pending);

    internet_radio_status_t radio = {0};
    internet_radio_get_status(&radio);
    cJSON *radio_json = cJSON_AddObjectToObject(root, "internet_radio");
    if (radio_json == NULL) {
        web_server_secure_zero(&settings, sizeof(settings));
        cJSON_Delete(root);
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    cJSON_AddStringToObject(radio_json, "state", web_server_radio_state_name(radio.state));
    cJSON_AddStringToObject(radio_json, "station", radio.station);
    cJSON_AddStringToObject(radio_json, "title", radio.title);
    cJSON_AddStringToObject(radio_json, "codec", radio.codec);
    cJSON_AddNumberToObject(radio_json, "bitrate_kbps", radio.bitrate_kbps);
    cJSON_AddNumberToObject(radio_json, "sample_rate_hz", radio.sample_rate_hz);
    cJSON_AddNumberToObject(radio_json, "station_count", internet_radio_station_count());
    for (uint8_t index = 0; index < settings.count; ++index) {
        cJSON_AddItemToArray(networks, cJSON_CreateString(settings.ssids[index]));
    }
    web_server_secure_zero(&settings, sizeof(settings));
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    httpd_resp_set_type(request, "application/json");
    const esp_err_t err = httpd_resp_sendstr(request, json);
    cJSON_free(json);
    return err;
}

static esp_err_t web_server_wifi_post(httpd_req_t *request)
{
    if (request->content_len <= 0 || request->content_len >= WEB_SERVER_REQUEST_MAX_LEN) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    char body[WEB_SERVER_REQUEST_MAX_LEN] = {0};
    int received = 0;
    while (received < request->content_len) {
        const int read = httpd_req_recv(request, body + received, request->content_len - received);
        if (read <= 0) {
            web_server_secure_zero(body, sizeof(body));
            httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Incomplete request");
            return ESP_FAIL;
        }
        received += read;
    }
    body[received] = '\0';
    wifi_network_t network = {0};
    const web_server_parse_result_t parse_result =
        web_server_parse_wifi_request(body, &network);
    web_server_secure_zero(body, sizeof(body));
    if (parse_result != WEB_SERVER_PARSE_OK) {
        web_server_secure_zero(&network, sizeof(network));
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "SSID or password is invalid");
        return ESP_FAIL;
    }
    const bool queued = web_socket_queue_wifi(&network);
    web_server_secure_zero(&network, sizeof(network));
    if (!queued) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Device is busy");
        return ESP_FAIL;
    }
    httpd_resp_set_status(request, "202 Accepted");
    return httpd_resp_send(request, NULL, 0);
}

static esp_err_t web_server_playlist_get(httpd_req_t *request)
{
    FILE *file = fopen(STATION_CATALOG_PATH, "r");
    if (file == NULL) {
        httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "Playlist not found");
        return ESP_FAIL;
    }
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(request, "Content-Disposition", "attachment; filename=\"playlist.csv\"");
    size_t bytes_read;
    while ((bytes_read = fread(s_file_chunk_buffer, 1, sizeof(s_file_chunk_buffer), file)) > 0) {
        const esp_err_t err = httpd_resp_send_chunk(request, s_file_chunk_buffer, bytes_read);
        if (err != ESP_OK) {
            fclose(file);
            return err;
        }
    }
    if (ferror(file)) {
        fclose(file);
        ESP_LOGE(TAG, "read playlist failed: %s", STATION_CATALOG_PATH);
        return ESP_FAIL;
    }
    fclose(file);
    return httpd_resp_send_chunk(request, NULL, 0);
}

static esp_err_t web_server_playlist_post(httpd_req_t *request)
{
    if (request->content_len <= 0 ||
        (size_t)request->content_len >= WEB_SERVER_PLAYLIST_MAX_LEN) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid playlist size");
        return ESP_FAIL;
    }
    char *body = malloc((size_t)request->content_len + 1U);
    if (body == NULL) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    size_t received = 0U;
    while (received < (size_t)request->content_len) {
        const int read = httpd_req_recv(request, body + received,
                                        (size_t)request->content_len - received);
        if (read <= 0) {
            free(body);
            httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Incomplete request");
            return ESP_FAIL;
        }
        received += (size_t)read;
    }
    body[received] = '\0';

    size_t count = 0U;
    bool active_station_removed = false;
    const esp_err_t result =
        internet_radio_catalog_replace(body, received, &count, &active_station_removed);
    free(body);

    if (result == ESP_OK && active_station_removed) {
        // Hand the stop to player_control instead of calling
        // internet_radio_stop() here: it waits seconds for the decoder task,
        // and this handler runs on the single HTTP worker task that also
        // serves every other request and WebSocket broadcast.
        const player_command_t stop = {
            .kind = PLAYER_COMMAND_STOP_SOURCE,
            .source = AUDIO_SOURCE_NONE,
            .item_index = PLAYER_ITEM_NONE,
        };
        if (!player_control_post(&stop)) {
            ESP_LOGW(TAG, "playing station removed but stop command was not queued");
        }
    }

    if (result == ESP_ERR_INVALID_ARG) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Playlist has no valid stations");
        return ESP_FAIL;
    }
    if (result != ESP_OK) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save playlist");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL || cJSON_AddNumberToObject(root, "count", (double)count) == NULL) {
        cJSON_Delete(root);
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    httpd_resp_set_type(request, "application/json");
    const esp_err_t send_err = httpd_resp_sendstr(request, json);
    cJSON_free(json);
    return send_err;
}

esp_err_t web_server_start(void)
{
    if (s_server != NULL) {
        if (s_websocket_started) {
            return ESP_OK;
        }
        if (!s_websocket_recovery_required) {
            return ESP_ERR_INVALID_STATE;
        }
        // A previous start left the WebSocket subsystem in a stalled cleanup
        // state (its shutdown-wait timed out) while the HTTP server was kept
        // alive. Give the stalled cleanup another, longer chance to finish
        // via web_socket_stop() before giving up again, so this call can
        // actually recover instead of failing forever.
        const esp_err_t recovery_result = web_socket_stop();
        if (recovery_result != ESP_OK) {
            ESP_LOGE(TAG, "WebSocket recovery still pending err=%s",
                     esp_err_to_name(recovery_result));
            return recovery_result;
        }
        s_websocket_recovery_required = false;
    } else {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.stack_size = 6144;
        config.max_uri_handlers = 16;
        config.max_open_sockets = WEB_SOCKET_SERVER_SOCKET_CAPACITY;
        config.send_wait_timeout = 1;
        config.lru_purge_enable = false;
        ESP_RETURN_ON_ERROR(httpd_start(&s_server, &config), TAG, "start HTTP server");
        const httpd_uri_t handlers[] = {
            {.uri = "/", .method = HTTP_GET, .handler = web_server_root_get},
            {.uri = "/app.js", .method = HTTP_GET, .handler = web_server_app_js_get},
            {.uri = "/style.css", .method = HTTP_GET, .handler = web_server_style_get},
            {.uri = "/settings", .method = HTTP_GET, .handler = web_server_settings_get},
            {.uri = "/settings.js", .method = HTTP_GET, .handler = web_server_settings_js_get},
            {.uri = "/playlist", .method = HTTP_GET, .handler = web_server_playlist_page_get},
            {.uri = "/playlist.js", .method = HTTP_GET, .handler = web_server_playlist_js_get},
            {.uri = "/api/status", .method = HTTP_GET, .handler = web_server_status_get},
            {.uri = "/api/wifi", .method = HTTP_POST, .handler = web_server_wifi_post},
            {.uri = "/api/playlist", .method = HTTP_GET, .handler = web_server_playlist_get},
            {.uri = "/api/playlist", .method = HTTP_POST, .handler = web_server_playlist_post},
        };
        for (size_t index = 0; index < sizeof(handlers) / sizeof(handlers[0]); ++index) {
            const esp_err_t err = httpd_register_uri_handler(s_server, &handlers[index]);
            if (err != ESP_OK) {
                httpd_stop(s_server);
                s_server = NULL;
                return err;
            }
        }
    }
    const esp_err_t websocket_result = web_socket_start(s_server);
    if (websocket_result != ESP_OK) {
        if (websocket_result != ESP_ERR_TIMEOUT) {
            httpd_stop(s_server);
            s_server = NULL;
        } else {
            s_websocket_recovery_required = true;
            ESP_LOGE(TAG, "WebSocket startup cleanup timed out; HTTP server retained");
        }
        return websocket_result;
    }
    s_websocket_started = true;
    s_websocket_recovery_required = false;
    ESP_LOGI(TAG, "HTTP server started");
    return ESP_OK;
}

esp_err_t web_server_stop(void)
{
    if (s_server == NULL) {
        s_websocket_started = false;
        return ESP_OK;
    }
    if (s_websocket_started || s_websocket_recovery_required) {
        const esp_err_t websocket_result = web_socket_stop();
        if (websocket_result != ESP_OK) {
            ESP_LOGE(TAG, "WebSocket stop failed; HTTP server retained err=%s",
                     esp_err_to_name(websocket_result));
            return websocket_result;
        }
        s_websocket_started = false;
        s_websocket_recovery_required = false;
    }
    const esp_err_t result = httpd_stop(s_server);
    if (result == ESP_OK) {
        s_server = NULL;
    }
    return result;
}
#endif
