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
#include "file_storage.h"
#include "web_json.h"
#include "yandex_auth.h"
#include "yandex_catalog.h"

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


web_server_yandex_action_t web_server_parse_yandex_action(const char *request)
{
    if (request == NULL) {
        return WEB_SERVER_YANDEX_ACTION_INVALID;
    }
    const char *cursor = request;
    char action[16] = {0};
    bool seen_action = false;
    json_skip_whitespace(&cursor);
    if (*cursor++ != '{') {
        return WEB_SERVER_YANDEX_ACTION_INVALID;
    }
    json_skip_whitespace(&cursor);
    while (*cursor != '}') {
        char key[8] = {0};
        if (!json_parse_string(&cursor, key, sizeof(key))) {
            return WEB_SERVER_YANDEX_ACTION_INVALID;
        }
        json_skip_whitespace(&cursor);
        if (*cursor++ != ':') {
            return WEB_SERVER_YANDEX_ACTION_INVALID;
        }
        json_skip_whitespace(&cursor);
        if (strcmp(key, "action") != 0 || seen_action ||
            !json_parse_string(&cursor, action, sizeof(action))) {
            return WEB_SERVER_YANDEX_ACTION_INVALID;
        }
        seen_action = true;
        json_skip_whitespace(&cursor);
        if (*cursor == ',') {
            ++cursor;
            json_skip_whitespace(&cursor);
            if (*cursor == '}') {
                return WEB_SERVER_YANDEX_ACTION_INVALID;
            }
        } else if (*cursor != '}') {
            return WEB_SERVER_YANDEX_ACTION_INVALID;
        }
    }
    ++cursor;
    json_skip_whitespace(&cursor);
    if (*cursor != '\0' || !seen_action) {
        return WEB_SERVER_YANDEX_ACTION_INVALID;
    }
    if (strcmp(action, "begin") == 0) return WEB_SERVER_YANDEX_ACTION_BEGIN;
    if (strcmp(action, "cancel") == 0) return WEB_SERVER_YANDEX_ACTION_CANCEL;
    if (strcmp(action, "forget") == 0) return WEB_SERVER_YANDEX_ACTION_FORGET;
    if (strcmp(action, "refresh") == 0) return WEB_SERVER_YANDEX_ACTION_REFRESH;
    return WEB_SERVER_YANDEX_ACTION_INVALID;
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

/* The USB listing goes over REST rather than the WebSocket because a directory
 * of up to FILE_STORAGE_MAX_ENTRIES names, each up to FILE_BROWSER_NAME_MAX_LEN
 * bytes, dwarfs WEB_PROTOCOL_EVENT_MAX - and growing that static frame buffer
 * is the wrong trade in a firmware already short of internal SRAM. Entries are
 * streamed one at a time through the shared chunk buffer, so the response size
 * is bounded by nothing at all.
 *
 * The path and revision are echoed back deliberately: the browser can open two
 * directories in quick succession, and without them a late reply for the first
 * would overwrite the second on screen. */
static const char *web_server_yandex_state_name(yandex_auth_state_t state)
{
    switch (state) {
    case YANDEX_AUTH_REQUESTING:
        return "requesting";
    case YANDEX_AUTH_WAITING:
        return "waiting";
    case YANDEX_AUTH_AUTHORIZED:
        return "authorized";
    case YANDEX_AUTH_FAILED:
        return "failed";
    case YANDEX_AUTH_IDLE:
    default:
        return "idle";
    }
}

static const char *web_server_yandex_error_name(yandex_auth_error_t error)
{
    switch (error) {
    case YANDEX_AUTH_ERROR_NETWORK:
        return "network";
    case YANDEX_AUTH_ERROR_TIMEOUT:
        return "timeout";
    case YANDEX_AUTH_ERROR_DENIED:
        return "denied";
    case YANDEX_AUTH_ERROR_SERVER:
        return "server";
    case YANDEX_AUTH_ERROR_STORAGE:
        return "storage";
    case YANDEX_AUTH_ERROR_NONE:
    default:
        return "none";
    }
}

static const char *web_server_yandex_catalog_name(yandex_catalog_state_t state)
{
    switch (state) {
    case YANDEX_CATALOG_LOADING:
        return "loading";
    case YANDEX_CATALOG_READY:
        return "ready";
    case YANDEX_CATALOG_FAILED:
        return "failed";
    case YANDEX_CATALOG_EMPTY:
    default:
        return "empty";
    }
}

static esp_err_t web_server_yandex_get(httpd_req_t *request)
{
    const yandex_auth_status_t status = yandex_auth_get_status();
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    cJSON_AddStringToObject(root, "state", web_server_yandex_state_name(status.state));
    cJSON_AddStringToObject(root, "error", web_server_yandex_error_name(status.error));
    /* The code and the page are meant to be read off this screen - that is the
     * whole point of the flow. The token never appears here: this interface has
     * no authentication, so everything it serves is public on the LAN. */
    cJSON_AddStringToObject(root, "user_code", status.user_code);
    cJSON_AddStringToObject(root, "verification_url", status.verification_url);
    cJSON_AddNumberToObject(root, "seconds_left", status.seconds_left);
    cJSON_AddStringToObject(root, "catalog",
                            web_server_yandex_catalog_name(yandex_catalog_get_state()));

    /* Over REST, like the rest of this endpoint: a dozen station names are far
     * past the 512-byte WebSocket frame, and the list changes once per visit
     * rather than continuously. */
    cJSON *stations = cJSON_AddArrayToObject(root, "stations");
    if (stations == NULL) {
        cJSON_Delete(root);
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    const size_t count = yandex_catalog_count();
    for (size_t index = 0; index < count; ++index) {
        yandex_station_t station;
        if (!yandex_catalog_station_at(index, &station)) break;
        cJSON *item = cJSON_CreateObject();
        if (item == NULL) break;
        cJSON_AddStringToObject(item, "id", station.id);
        cJSON_AddStringToObject(item, "name", station.name);
        cJSON_AddItemToArray(stations, item);
    }

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

static esp_err_t web_server_yandex_post(httpd_req_t *request)
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
            httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Incomplete request");
            return ESP_FAIL;
        }
        received += read;
    }
    body[received] = '\0';

    esp_err_t result = ESP_ERR_INVALID_ARG;
    switch (web_server_parse_yandex_action(body)) {
    case WEB_SERVER_YANDEX_ACTION_BEGIN:
        result = yandex_auth_begin();
        break;
    case WEB_SERVER_YANDEX_ACTION_CANCEL:
        result = yandex_auth_cancel();
        break;
    case WEB_SERVER_YANDEX_ACTION_FORGET:
        result = yandex_auth_forget();
        break;
    case WEB_SERVER_YANDEX_ACTION_REFRESH:
        result = yandex_catalog_request_refresh();
        break;
    case WEB_SERVER_YANDEX_ACTION_INVALID:
    default:
        break;
    }
    if (result == ESP_ERR_INVALID_ARG) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Unknown action");
        return ESP_FAIL;
    }
    if (result == ESP_ERR_INVALID_STATE) {
        /* Asking for stations with no account linked is a request that does
         * not apply, not a device that is struggling - and "busy" would send
         * whoever reads it looking for the wrong problem. */
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Account is not linked");
        return ESP_FAIL;
    }
    if (result != ESP_OK) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Device is busy");
        return ESP_FAIL;
    }
    /* Accepted, not done: the exchange takes as long as the user takes to type
     * the code, and the page follows it through GET /api/yandex. */
    httpd_resp_set_status(request, "202 Accepted");
    return httpd_resp_send(request, NULL, 0);
}

static esp_err_t web_server_files_get(httpd_req_t *request)
{
    char path[FILE_BROWSER_PATH_MAX_LEN];
    if (!file_storage_current_path(path, sizeof(path))) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Listing unavailable");
        return ESP_FAIL;
    }
    /* Asked of the volume the listing is actually on rather than of the drive:
     * the same listing serves the card, and a mounted stick is no reason to
     * hand out a directory of a card that has been released. */
    if (!file_storage_path_mounted(path)) {
        httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "No media");
        return ESP_FAIL;
    }

    web_json_writer_t writer;
    web_json_init(&writer, s_file_chunk_buffer, sizeof(s_file_chunk_buffer),
                  sizeof(s_file_chunk_buffer));
    web_json_literal(&writer, "{\"path\":");
    web_json_string(&writer, path);
    web_json_literal(&writer, ",\"revision\":");
    web_json_format(&writer, "%u", player_control_listing_revision());
    web_json_literal(&writer, ",\"has_parent\":");
    web_json_literal(&writer, file_browser_path_is_root(path) ? "false" : "true");
    web_json_literal(&writer, ",\"items\":[");
    if (!web_json_valid(&writer)) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Listing too large");
        return ESP_FAIL;
    }
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    // The listing is live state, not a file: a cached copy would show a
    // directory the device has already left.
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");

    const size_t count = file_storage_entry_count();
    for (size_t index = 0U; index < count; ++index) {
        file_browser_entry_t entry;
        if (!file_storage_entry_at(index, &entry)) {
            // The drive was pulled mid-walk. Stop cleanly rather than emit a
            // document that claims more entries than it carries.
            break;
        }
        web_json_literal(&writer, index > 0U ? ",{\"index\":" : "{\"index\":");
        web_json_format(&writer, "%u", (unsigned)index);
        web_json_literal(&writer, ",\"name\":");
        web_json_string(&writer, entry.name);
        web_json_literal(&writer, ",\"kind\":");
        web_json_literal(&writer,
                         entry.kind == FILE_BROWSER_ENTRY_DIRECTORY ? "\"dir\""
                                                                   : "\"file\"");
        if (entry.kind != FILE_BROWSER_ENTRY_DIRECTORY) {
            web_json_literal(&writer, ",\"format\":");
            web_json_string(&writer, file_browser_format_name(entry.format));
        }
        web_json_literal(&writer, "}");
        if (!web_json_valid(&writer)) {
            ESP_LOGE(TAG, "USB listing entry %u did not fit the chunk buffer",
                     (unsigned)index);
            return ESP_FAIL;
        }
        // Flush whenever the next entry might not fit, so one buffer serves a
        // directory of any size.
        if (web_json_length(&writer) + sizeof(file_browser_entry_t) >
            sizeof(s_file_chunk_buffer)) {
            const esp_err_t err = httpd_resp_send_chunk(request, s_file_chunk_buffer,
                                                        web_json_length(&writer));
            if (err != ESP_OK) return err;
            web_json_init(&writer, s_file_chunk_buffer, sizeof(s_file_chunk_buffer),
                          sizeof(s_file_chunk_buffer));
        }
    }

    web_json_literal(&writer, "]}");
    if (!web_json_valid(&writer)) return ESP_FAIL;
    const esp_err_t err = httpd_resp_send_chunk(request, s_file_chunk_buffer,
                                                web_json_length(&writer));
    if (err != ESP_OK) return err;
    return httpd_resp_send_chunk(request, NULL, 0);
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
        // The HTTP worker is network-bound; keep it on core 0 with Wi-Fi and
        // lwIP so it cannot preempt the audio decoder pinned to core 1.
        config.core_id = 0;
        // Fifteen are registered below plus /ws; the spare four exist because
        // running out is not a build error - httpd_register_uri_handler fails
        // at startup and takes the whole web server down with it.
        config.max_uri_handlers = 20;
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
            {.uri = "/api/files", .method = HTTP_GET, .handler = web_server_files_get},
            {.uri = "/api/playlist", .method = HTTP_GET, .handler = web_server_playlist_get},
            {.uri = "/api/playlist", .method = HTTP_POST, .handler = web_server_playlist_post},
            {.uri = "/api/yandex", .method = HTTP_GET, .handler = web_server_yandex_get},
            {.uri = "/api/yandex", .method = HTTP_POST, .handler = web_server_yandex_post},
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
