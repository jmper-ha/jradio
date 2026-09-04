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
#include <strings.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_http_server.h"

#include "wifi_provisioning.h"
#include "internet_radio.h"
#include "player_control.h"
#include "station_catalog.h"
#include "ui_now_playing.h"
#include "file_storage.h"
#include "album_art.h"
#include "image_decode.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include "ui_menu.h"
#include "web_cover.h"
#include "web_json.h"
#include "web_settings.h"
#include "yandex_auth.h"
#include "yandex_catalog.h"
#include "yandex_rotor.h"

#define WEB_SERVER_MOUNT_PATH "/littlefs"
#define WEB_SERVER_WEB_ROOT "www"
#define WEB_SERVER_REQUEST_MAX_LEN 256
/* The playlist editor's test request carries a whole station: a 256-byte URL,
 * a 96-byte name and the JSON around them. */
#define WEB_SERVER_STATION_TEST_MAX_LEN 512
/* A picture already scaled to ALBUM_ART_MAX_SIZE by the browser. PNG of a
 * 160 px square is a few kilobytes; the cap is there to stop a mistake, not to
 * fit a photograph. */
#define WEB_SERVER_STATION_ICON_MAX_LEN 32768U
/* The upload has to be able to carry a full catalogue, so it is derived from
 * the catalogue rather than typed - otherwise raising the station count leaves
 * a limit behind that rejects the very file the device can now hold. */
#define WEB_SERVER_PLAYLIST_MAX_LEN STATION_CATALOG_TEXT_MAX_LEN

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

/* A tag for what the file currently holds, so a page load asks "still this?"
 * instead of "give me it again". FNV-1a over the bytes rather than the size or
 * a timestamp: the image is built on a PC and flashed whole, so mtimes say
 * nothing useful, and an edit that leaves the size alone is exactly the change
 * that must not slip through. */
static bool web_server_file_tag(FILE *file, char *out, size_t out_size)
{
    uint32_t hash = 2166136261U;
    size_t total = 0U;
    size_t read;
    while ((read = fread(s_file_chunk_buffer, 1U, sizeof(s_file_chunk_buffer), file)) > 0U) {
        for (size_t index = 0U; index < read; ++index) {
            hash ^= (uint8_t)s_file_chunk_buffer[index];
            hash *= 16777619U;
        }
        total += read;
    }
    if (ferror(file) || fseek(file, 0, SEEK_SET) != 0) return false;
    const int written = snprintf(out, out_size, "\"%x-%x\"", (unsigned)total, (unsigned)hash);
    return written > 0 && (size_t)written < out_size;
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
    /* Kept in scope until the response has gone out: esp_http_server stores the
     * pointer it is given and reads it while sending. */
    char tag[24];
    const bool tagged = web_server_file_tag(file, tag, sizeof(tag));

    httpd_resp_set_type(request, content_type);
    if (gzipped) {
        httpd_resp_set_hdr(request, "Content-Encoding", "gzip");
    }
    /* Kept, but never used without asking. These assets change only when the
     * partition is re-flashed, and a browser holding an hour-old copy of app.js
     * after that is a page running against a device that has moved on - which
     * is a whole class of "the screen shows something else" that no amount of
     * fixing the device can reach. Revalidation costs one conditional GET per
     * asset per page load on a LAN; the 304 carries no body. */
    httpd_resp_set_hdr(request, "Cache-Control", "no-cache");
    // The gzipped and plain variants of one URL are different bytes.
    httpd_resp_set_hdr(request, "Vary", "Accept-Encoding");
    if (tagged) {
        httpd_resp_set_hdr(request, "ETag", tag);
        char have[24];
        if (httpd_req_get_hdr_value_str(request, "If-None-Match", have, sizeof(have)) == ESP_OK &&
            strcmp(have, tag) == 0) {
            fclose(file);
            httpd_resp_set_status(request, "304 Not Modified");
            return httpd_resp_send(request, NULL, 0);
        }
    }
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

static esp_err_t web_server_theme_js_get(httpd_req_t *request)
{
    return web_server_send_file(request, WEB_SERVER_WEB_ROOT "/theme.js",
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
    /* The same answer the panel and the page give: status.station carries the
     * stream's icy-name, and the playlist's third column says whose name to
     * show. Without this the diagnostic request named the station differently
     * from both screens. */
    const station_catalog_entry_t *station_entry =
        player_control_station_at(radio.station_index);
    cJSON_AddStringToObject(
        radio_json, "station",
        ui_now_playing_station_name(station_entry != NULL && station_entry->flag != 0,
                                    station_entry != NULL ? station_entry->name : "",
                                    radio.station));
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
    const bool queued = web_socket_queue_wifi(WEB_COMMAND_WIFI_SAVE, &network);
    web_server_secure_zero(&network, sizeof(network));
    if (!queued) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Device is busy");
        return ESP_FAIL;
    }
    httpd_resp_set_status(request, "202 Accepted");
    return httpd_resp_send(request, NULL, 0);
}

/* Looking for a network to join. Two calls rather than one because a scan takes
 * a few seconds and esp_http_server has a single worker task: a handler that
 * waited for the result would hold up the cover, the track position and every
 * other request behind it. The POST starts one, the GET asks whether it is done.
 *
 * Both refuse unless the setup AP is up. That is the only mode where a scan is
 * free: nothing is playing, and the channel hopping it does costs nothing but
 * a short stall of this very page. */
static esp_err_t web_server_wifi_scan_post(httpd_req_t *request)
{
    const esp_err_t err = wifi_provisioning_scan_start();
    if (err == ESP_ERR_INVALID_STATE) {
        // esp_http_server has no 409 in httpd_err_code_t, and this is not one
        // of the shapes httpd_resp_send_err() knows how to explain anyway.
        httpd_resp_set_status(request, "409 Conflict");
        httpd_resp_set_type(request, "application/json; charset=utf-8");
        return httpd_resp_sendstr(request, "{\"state\":\"unavailable\"}");
    }
    if (err != ESP_OK) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Scan failed to start");
        return ESP_FAIL;
    }
    httpd_resp_set_status(request, "202 Accepted");
    return httpd_resp_send(request, NULL, 0);
}

static esp_err_t web_server_wifi_scan_get(httpd_req_t *request)
{
    /* Handlers run one at a time on the single server task, so this is a
     * static rather than 840 bytes of a 6 KB stack. */
    static wifi_provisioning_scan_entry_t entries[WIFI_PROVISIONING_SCAN_MAX];
    uint8_t count = 0U;
    const esp_err_t err = wifi_provisioning_scan_result(entries, (uint8_t)(sizeof(entries) /
                                                                          sizeof(entries[0])),
                                                        &count);
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    web_json_writer_t writer;
    web_json_init(&writer, s_file_chunk_buffer, sizeof(s_file_chunk_buffer),
                  sizeof(s_file_chunk_buffer));
    if (err != ESP_OK) {
        /* "idle" is not an error: nothing has been asked for yet, or the last
           answer has already been handed over. The page presses the button. */
        const char *state = err == ESP_ERR_NOT_FINISHED  ? "scanning"
                            : err == ESP_ERR_INVALID_STATE ? "idle"
                                                           : "failed";
        web_json_literal(&writer, "{\"state\":");
        web_json_string(&writer, state);
        web_json_literal(&writer, ",\"networks\":[]}");
        return web_json_valid(&writer)
                   ? httpd_resp_send(request, s_file_chunk_buffer, web_json_length(&writer))
                   : ESP_FAIL;
    }
    web_json_literal(&writer, "{\"state\":\"done\",\"networks\":[");
    for (uint8_t index = 0U; index < count; ++index) {
        web_json_literal(&writer, index > 0U ? ",{\"ssid\":" : "{\"ssid\":");
        web_json_string(&writer, entries[index].ssid);
        web_json_literal(&writer, ",\"rssi\":");
        web_json_format(&writer, "%d", (int)entries[index].rssi);
        web_json_literal(&writer, ",\"secure\":");
        web_json_literal(&writer, entries[index].secure ? "true}" : "false}");
        if (!web_json_valid(&writer)) return ESP_FAIL;
        /* Every character of a name can escape to six bytes, so flush before
           the next one might not fit - the same rule the station list uses. */
        if (web_json_length(&writer) + (WIFI_SETTINGS_SSID_MAX_LEN * 6U) + 48U >
            sizeof(s_file_chunk_buffer)) {
            const esp_err_t sent = httpd_resp_send_chunk(request, s_file_chunk_buffer,
                                                         web_json_length(&writer));
            if (sent != ESP_OK) return sent;
            web_json_init(&writer, s_file_chunk_buffer, sizeof(s_file_chunk_buffer),
                          sizeof(s_file_chunk_buffer));
        }
    }
    web_json_literal(&writer, "]}");
    if (!web_json_valid(&writer)) return ESP_FAIL;
    const esp_err_t sent = httpd_resp_send_chunk(request, s_file_chunk_buffer,
                                                 web_json_length(&writer));
    if (sent != ESP_OK) return sent;
    return httpd_resp_send_chunk(request, NULL, 0);
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
        // The same short name the device's own row shows: inside a playlist
        // the entry name is the path the file wrote.
        web_json_string(&writer, file_browser_display_name(entry.name));
        web_json_literal(&writer, ",\"kind\":");
        // A playlist is its own kind rather than a directory, because the page
        // shows the type ("M3U") beside it and a directory has none.
        web_json_literal(&writer, entry.kind == FILE_BROWSER_ENTRY_DIRECTORY ? "\"dir\""
                                  : entry.kind == FILE_BROWSER_ENTRY_PLAYLIST ? "\"playlist\""
                                                                              : "\"file\"");
        if (entry.kind != FILE_BROWSER_ENTRY_DIRECTORY) {
            web_json_literal(&writer, ",\"format\":");
            web_json_string(&writer, file_browser_entry_type_label(&entry));
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

/* The names behind the socket's list header.
 *
 * The frame carries a kind, a count, an active index and a revision, and no
 * labels at all - see write_list() in web_socket.c for why. This is where the
 * labels come from, and the browser re-fetches whenever the revision moves.
 *
 * Which list depends on the source, exactly as the on-device screen does: both
 * are flat lists of stations, but one is the catalogue file and the other is
 * fetched from the account. The source is read from a single snapshot so it
 * cannot disagree with the count taken beside it. */
static esp_err_t web_server_stations_get(httpd_req_t *request)
{
    player_snapshot_t snapshot;
    player_control_get_snapshot(&snapshot);
    const bool rotor = snapshot.active_source == AUDIO_SOURCE_YANDEX;
    const size_t count = rotor ? yandex_catalog_count() : internet_radio_station_count();

    web_json_writer_t writer;
    web_json_init(&writer, s_file_chunk_buffer, sizeof(s_file_chunk_buffer),
                  sizeof(s_file_chunk_buffer));
    /* The source is named outright: the radio and the rotor share one list
     * kind, and the rotor's catalogue is fetched over the network - a reply to
     * a request sent before the switch arrives after it. */
    web_json_literal(&writer, "{\"kind\":\"stations\",\"source\":");
    web_json_string(&writer, rotor ? "yandex" : "internet_radio");
    web_json_literal(&writer, ",\"revision\":");
    web_json_format(&writer, "%u", snapshot.listing_revision);
    web_json_literal(&writer, ",\"count\":");
    web_json_format(&writer, "%u", (unsigned)count);
    web_json_literal(&writer, ",\"items\":[");
    if (!web_json_valid(&writer)) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Listing too large");
        return ESP_FAIL;
    }
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    // Live state, not a file: a cached copy would name stations the device has
    // already been given a new playlist for.
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");

    for (size_t index = 0U; index < count; ++index) {
        const char *label = "";
        yandex_station_t station;
        if (rotor) {
            if (!yandex_catalog_station_at(index, &station)) break;
            label = station.name;
        } else {
            const station_catalog_entry_t *entry = internet_radio_station_at(index);
            if (entry == NULL) break;
            label = entry->name;
        }
        web_json_literal(&writer, index > 0U ? ",{\"index\":" : "{\"index\":");
        web_json_format(&writer, "%u", (unsigned)index);
        web_json_literal(&writer, ",\"label\":");
        web_json_string(&writer, label);
        web_json_literal(&writer, "}");
        if (!web_json_valid(&writer)) {
            ESP_LOGE(TAG, "station %u did not fit the chunk buffer", (unsigned)index);
            return ESP_FAIL;
        }
        /* Flush whenever the next entry might not fit, so one buffer serves a
         * catalogue of any size - the same rule the file listing uses, and the
         * reason 99 stations need no bigger buffer than 32 did. */
        if (web_json_length(&writer) + STATION_CATALOG_NAME_MAX_LEN + 32U >
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

/* Defined below, beside the upload it cleans up after. */
static void web_server_sweep_station_icons(void);

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
    if (result == ESP_OK) {
        // The catalogue is the list of names in use, so this is the moment a
        // picture nobody refers to can be told from one in use.
        web_server_sweep_station_icons();
    }

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

    /* The names are no longer in the socket frame, so nothing else would tell
     * an open browser that they changed - a playlist edited to the same number
     * of stations looks identical in every other field of the snapshot. */
    player_control_note_listing_changed();

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

/* What the device's own menu would offer. The Yandex row exists only in a
 * build that has the feature, and a home screen only where there is more than
 * one place to go - which the Yandex switch itself can decide, since turning it
 * off can leave a board with nothing but the radio and the settings.
 *
 * Asked of ui_menu because that is where the answer already lives: the home
 * screen, the web interface and autoplay all have to agree about which sources
 * this build has. */
bool web_server_yandex_available(void)
{
    return ui_menu_item_is_visible(UI_MENU_ITEM_YANDEX_MUSIC, true);
}

bool web_server_home_screen_available(bool yandex_enabled)
{
    return ui_menu_home_screen_needed(ui_menu_visible_count(yandex_enabled));
}

static esp_err_t web_server_settings_api_get(httpd_req_t *request)
{
    /* Read back off the card rather than from a copy held here: the UI task
     * owns the settings, and the only thing both tasks certainly agree on is
     * the file. Brightness and volume settle a second or two after the knob
     * stops, so a page opened mid-turn can show the value from just before it
     * - which is the same lag the on-device screen has. */
    device_settings_t settings;
    if (!device_settings_init(&settings)) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Settings unavailable");
        return ESP_FAIL;
    }
    web_settings_view_t view;
    web_settings_make_view(&view, &settings,
                           web_server_home_screen_available(settings.yandex_music),
                           web_server_yandex_available());
    const size_t length = web_settings_serialize(s_file_chunk_buffer,
                                                 sizeof(s_file_chunk_buffer),
                                                 &view);
    if (length == 0U) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Settings too large");
        return ESP_FAIL;
    }
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, s_file_chunk_buffer, (ssize_t)length);
}

/* Plays a URL that is in no catalogue: the playlist editor's "test on the
 * device", which lets a station be heard before it is saved. Nothing is
 * written - not the catalogue, not the resume point - so a station that turns
 * out to be dead leaves no trace. An empty URL stops the test. */
/* The picture of a station, uploaded from the playlist editor. The browser has
 * already scaled it to ALBUM_ART_MAX_SIZE and re-encoded it, so what arrives is a
 * few kilobytes of PNG - the device only stores the bytes and hands them to
 * album_art when the station plays, which is the same decoder a file's cover
 * goes through.
 *
 * The name is generated here rather than taken from the request: the file name
 * ends up in the playlist and is joined to one directory, and a name chosen by
 * the client is a name that can be a path. */
static esp_err_t web_server_station_icon_post(httpd_req_t *request)
{
    if (request->content_len <= 0 ||
        (size_t)request->content_len > WEB_SERVER_STATION_ICON_MAX_LEN) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid picture size");
        return ESP_FAIL;
    }
    if (mkdir(STATION_ICON_DIR, 0777) != 0 && errno != EEXIST) {
        ESP_LOGE(TAG, "cannot create %s: %s", STATION_ICON_DIR, strerror(errno));
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Storage unavailable");
        return ESP_FAIL;
    }

    /* Streamed in kilobyte pieces: the whole picture never sits in RAM here,
     * and the HTTP worker's stack is the scarce thing on this path. */
    char chunk[1024];
    /* The signature is read before the file is opened rather than streamed
     * straight into it, because it is what picks the extension. The panel
     * decodes by signature and would not care what the file is called, but the
     * picture is also handed back over HTTP, and a JPEG served as image/png is
     * a broken picture in the browser. It doubles as the only check there is
     * that this is a picture at all: without it anything at all could be
     * stored, and the panel found out at the far end with "could not be
     * decoded". */
    size_t received = 0U;
    while (received < 8U && received < (size_t)request->content_len) {
        const int read = httpd_req_recv(request, chunk + received, 8U - received);
        if (read <= 0) {
            httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Incomplete upload");
            return ESP_FAIL;
        }
        received += (size_t)read;
    }
    const char *extension = NULL;
    if (image_decode_is_png((const uint8_t *)chunk, received)) {
        extension = "png";
    } else if (received >= 3U && (unsigned char)chunk[0] == 0xFFU &&
               (unsigned char)chunk[1] == 0xD8U && (unsigned char)chunk[2] == 0xFFU) {
        extension = "jpg";
    }
    if (extension == NULL) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Unsupported picture format");
        return ESP_FAIL;
    }

    char name[STATION_CATALOG_ICON_MAX_LEN];
    char path[sizeof(STATION_ICON_DIR) + STATION_CATALOG_ICON_MAX_LEN + 1U];
    FILE *file = NULL;
    /* A few attempts rather than one: the name is random, and the only way to
     * find out it is taken is to look. */
    for (int attempt = 0; attempt < 8 && file == NULL; ++attempt) {
        snprintf(name, sizeof(name), "s%08lx.%s", (unsigned long)esp_random(), extension);
        snprintf(path, sizeof(path), "%s/%s", STATION_ICON_DIR, name);
        struct stat existing;
        if (stat(path, &existing) == 0) continue;
        file = fopen(path, "wb");
    }
    if (file == NULL) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Cannot store picture");
        return ESP_FAIL;
    }

    bool failed = fwrite(chunk, 1U, received, file) != received;
    while (!failed && received < (size_t)request->content_len) {
        const size_t want = (size_t)request->content_len - received;
        const int read = httpd_req_recv(request, chunk,
                                        want < sizeof(chunk) ? want : sizeof(chunk));
        if (read <= 0) {
            failed = true;
            break;
        }
        if (fwrite(chunk, 1U, (size_t)read, file) != (size_t)read) {
            failed = true;
            break;
        }
        received += (size_t)read;
    }
    fclose(file);
    if (failed) {
        (void)unlink(path);
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Incomplete upload");
        return ESP_FAIL;
    }

    char reply[STATION_CATALOG_ICON_MAX_LEN + 16U];
    snprintf(reply, sizeof(reply), "{\"file\":\"%s\"}", name);
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, reply);
}

/* Hands a stored picture back, so the editor can show what a station already
 * has after the page is reloaded. The name is checked against the same rule
 * the catalogue parses it with - it is joined to one directory, and a name
 * that is a path is a way out of it. */
static esp_err_t web_server_station_icon_get(httpd_req_t *request)
{
    char query[STATION_CATALOG_ICON_MAX_LEN + 16U];
    char name[STATION_CATALOG_ICON_MAX_LEN];
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "file", name, sizeof(name)) != ESP_OK ||
        !station_catalog_icon_is_valid(name) || name[0] == '\0') {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid picture name");
        return ESP_FAIL;
    }
    /* Relative to the mount, which is what web_server_send_file() joins to -
     * and it is also what applies the same caching every other stored file
     * gets. A new picture is a new name, so a cached one is never stale. */
    char relative[sizeof(STATION_ICON_DIR) + STATION_CATALOG_ICON_MAX_LEN];
    snprintf(relative, sizeof(relative), "radio_img/%s", name);
    /* By the name, because that is all this handler has: the extension was
     * chosen from the signature when the picture was stored. */
    const char *dot = strrchr(name, '.');
    const char *type = (dot != NULL && (strcasecmp(dot, ".jpg") == 0 ||
                                        strcasecmp(dot, ".jpeg") == 0))
                           ? "image/jpeg"
                           : "image/png";
    return web_server_send_file(request, relative, type);
}

/* Pictures nobody names any more are deleted when the playlist is saved. That
 * is the only moment the full set of names is known, and without it a picture
 * replaced or a station removed would keep its file for ever. */
static void web_server_sweep_station_icons(void)
{
    DIR *dir = opendir(STATION_ICON_DIR);
    if (dir == NULL) return;
    const struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) continue;
        if (internet_radio_icon_in_use(entry->d_name)) continue;
        char path[sizeof(STATION_ICON_DIR) + STATION_CATALOG_ICON_MAX_LEN + 1U];
        if (snprintf(path, sizeof(path), "%s/%s", STATION_ICON_DIR, entry->d_name) >=
            (int)sizeof(path)) {
            continue;
        }
        if (unlink(path) == 0) {
            ESP_LOGI(TAG, "removed unused station picture %s", entry->d_name);
        }
    }
    closedir(dir);
}

static esp_err_t web_server_station_test_post(httpd_req_t *request)
{
    /* Its own size, and static rather than on the stack: a station URL alone
     * may be 256 bytes, which is the whole of WEB_SERVER_REQUEST_MAX_LEN, and
     * the name and the JSON around them still have to fit. esp_http_server
     * runs one worker, so this buffer has one user at a time. */
    static char body[WEB_SERVER_STATION_TEST_MAX_LEN];
    if (request->content_len <= 0 ||
        request->content_len >= (int)sizeof(body)) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    memset(body, 0, sizeof(body));
    int received = 0;
    while (received < request->content_len) {
        const int read = httpd_req_recv(request, body + received,
                                        request->content_len - received);
        if (read <= 0) {
            httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Incomplete request");
            return ESP_FAIL;
        }
        received += read;
    }

    cJSON *root = cJSON_ParseWithLength(body, (size_t)received);
    const cJSON *url = root == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(root, "url");
    if (!cJSON_IsString(url)) {
        cJSON_Delete(root);
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(root, "name");
    const bool posted = player_control_post_stream_test(
        url->valuestring, cJSON_IsString(name) ? name->valuestring : "");
    cJSON_Delete(root);
    if (!posted) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Address too long");
        return ESP_FAIL;
    }
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{\"ok\":true}");
}

static esp_err_t web_server_settings_api_post(httpd_req_t *request)
{
    if (request->content_len <= 0 || request->content_len >= WEB_SERVER_REQUEST_MAX_LEN) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    char body[WEB_SERVER_REQUEST_MAX_LEN] = {0};
    int received = 0;
    while (received < request->content_len) {
        const int read = httpd_req_recv(request, body + received,
                                        request->content_len - received);
        if (read <= 0) {
            httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Incomplete request");
            return ESP_FAIL;
        }
        received += read;
    }

    web_settings_change_t change;
    if (!web_settings_parse(body, (size_t)received, &change)) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Unknown setting");
        return ESP_FAIL;
    }
    /* Read, change, write - through the same setters the on-device screen
     * uses, so the two writers cannot disagree about the file's shape.
     * settings_csv serialises the read-modify-write, which is what makes a
     * third writer safe here. */
    device_settings_t settings;
    if (!device_settings_init(&settings)) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Settings unavailable");
        return ESP_FAIL;
    }
    if (!web_settings_apply(&settings, &change)) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Failed to save settings");
        return ESP_FAIL;
    }
    /* The value is on the card; making it true of the running device is the
     * UI task's job - it owns the backlight, the panel rotation, the volume
     * and the menus. */
    device_settings_mark_changed();
    return web_server_settings_api_get(request);
}

static esp_err_t web_server_progress_get(httpd_req_t *request)
{
    uint32_t elapsed = 0U;
    uint32_t total = 0U;
    const bool progress = player_control_track_progress(&elapsed, &total);
    uint8_t buffer_percent = 0U;
    const bool buffered = player_control_input_fill(&buffer_percent);
    const album_art_status_t cover = album_art_status();

    web_json_writer_t writer;
    web_json_init(&writer, s_file_chunk_buffer, sizeof(s_file_chunk_buffer),
                  sizeof(s_file_chunk_buffer));
    web_json_literal(&writer, "{");
    /* Absent rather than zero where the idea does not apply: a stream has no
     * end to run towards, and a bar drawn at 0% would claim it does. */
    if (progress) {
        web_json_literal(&writer, "\"elapsed_seconds\":");
        web_json_format(&writer, "%u", (unsigned)elapsed);
        web_json_literal(&writer, ",\"total_seconds\":");
        web_json_format(&writer, "%u", (unsigned)total);
        web_json_literal(&writer, ",");
    }
    if (buffered) {
        web_json_literal(&writer, "\"buffer_percent\":");
        web_json_format(&writer, "%u", (unsigned)buffer_percent);
        web_json_literal(&writer, ",");
    }
    /* The generation is what the page watches: the picture itself comes from
     * /api/cover, and re-fetching 27 KB once a second to find out it has not
     * changed is exactly what this number exists to prevent.
     *
     * The signature is what the page *names* the picture by. The generation
     * counts from zero at every boot, so ?g=1 meant a different cover on every
     * run while the answer was cached for a day - three browsers ended up
     * showing three different album covers for the same track. */
    web_json_literal(&writer, "\"cover\":{\"present\":");
    web_json_literal(&writer, cover.present ? "true" : "false");
    web_json_literal(&writer, ",\"generation\":");
    web_json_format(&writer, "%u", cover.generation);
    web_json_literal(&writer, ",\"signature\":");
    web_json_format(&writer, "%u", (unsigned)cover.signature);
    web_json_literal(&writer, ",\"width\":");
    web_json_format(&writer, "%u", (unsigned)cover.width);
    web_json_literal(&writer, ",\"height\":");
    web_json_format(&writer, "%u", (unsigned)cover.height);
    /* The rotor's tracks carry a picture the service will serve at any size,
     * so the page is given the address and fetches it at the size it draws,
     * instead of stretching the 96 px the panel decoded. Nothing else has an
     * address to give: a station icon and a file's tag live on the device. */
    char cover_url[YANDEX_COVER_URL_MAX + 1U];
    /* No source check beside it: the rotor answers only while it is the one
     * playing, and reading the snapshot here would copy it onto the http
     * worker's stack for one comparison. */
    if (yandex_rotor_playing_cover_url(cover_url, sizeof(cover_url))) {
        web_json_literal(&writer, ",\"url\":");
        web_json_string(&writer, cover_url);
    }
    web_json_literal(&writer, "}}");
    if (!web_json_valid(&writer)) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Progress unavailable");
        return ESP_FAIL;
    }
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, s_file_chunk_buffer,
                           (ssize_t)web_json_length(&writer));
}

static esp_err_t web_server_cover_get(httpd_req_t *request)
{
    const album_art_status_t status = album_art_status();
    if (!status.present || status.width == 0U || status.height == 0U) {
        httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "No cover");
        return ESP_FAIL;
    }
    /* PSRAM first, and freed before the handler returns: the largest free
     * *internal* block is what mbedtls needs for AES, and a cover held there
     * takes HTTPS stations down with it. */
    uint16_t *pixels = heap_caps_malloc(ALBUM_ART_PIXELS * sizeof(uint16_t),
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (pixels == NULL) {
        pixels = heap_caps_malloc(ALBUM_ART_PIXELS * sizeof(uint16_t),
                                  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (pixels == NULL) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    uint16_t width = 0U;
    uint16_t height = 0U;
    if (!album_art_copy(pixels, ALBUM_ART_PIXELS, &width, &height)) {
        heap_caps_free(pixels);
        // The track changed while this ran, and the next poll will ask again.
        httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "No cover");
        return ESP_FAIL;
    }

    const size_t stride = web_cover_bmp_stride(width);
    uint8_t *chunk = (uint8_t *)s_file_chunk_buffer;
    if (stride > sizeof(s_file_chunk_buffer) ||
        !web_cover_bmp_header(chunk, sizeof(s_file_chunk_buffer), width, height)) {
        heap_caps_free(pixels);
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Cover too large");
        return ESP_FAIL;
    }
    httpd_resp_set_type(request, "image/bmp");
    /* Never stored. The page already asks only when the picture changes - the
     * signature rides in the query string - so a cache saves at most one fetch
     * per track and can hand back the wrong picture for a whole day: two
     * browsers showed two different album covers for one track, each the one
     * it happened to cache first under an older page's ?g= address. Correct
     * beats 27 KB. */
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");

    size_t length = WEB_COVER_BMP_HEADER_SIZE;
    esp_err_t err = ESP_OK;
    // Bottom-up, which is the order the format stores rows in.
    for (uint16_t row = height; row > 0U && err == ESP_OK; --row) {
        if (length + stride > sizeof(s_file_chunk_buffer)) {
            err = httpd_resp_send_chunk(request, s_file_chunk_buffer, (ssize_t)length);
            length = 0U;
            if (err != ESP_OK) break;
        }
        if (!web_cover_bmp_row(chunk + length, sizeof(s_file_chunk_buffer) - length,
                               pixels + (size_t)(row - 1U) * width, width)) {
            err = ESP_FAIL;
            break;
        }
        length += stride;
    }
    heap_caps_free(pixels);
    if (err != ESP_OK) return err;
    if (length > 0U) {
        err = httpd_resp_send_chunk(request, s_file_chunk_buffer, (ssize_t)length);
        if (err != ESP_OK) return err;
    }
    return httpd_resp_send_chunk(request, NULL, 0);
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
        // Twenty-three are registered below plus /ws; the spare ones exist
        // because running out is not a build error - httpd_register_uri_handler
        // fails at startup and takes the whole web server down with it.
        config.max_uri_handlers = 30;
        config.max_open_sockets = WEB_SOCKET_SERVER_SOCKET_CAPACITY;
        config.send_wait_timeout = 1;
        config.lru_purge_enable = false;
        ESP_RETURN_ON_ERROR(httpd_start(&s_server, &config), TAG, "start HTTP server");
        const httpd_uri_t handlers[] = {
            {.uri = "/", .method = HTTP_GET, .handler = web_server_root_get},
            {.uri = "/app.js", .method = HTTP_GET, .handler = web_server_app_js_get},
            {.uri = "/theme.js", .method = HTTP_GET, .handler = web_server_theme_js_get},
            {.uri = "/style.css", .method = HTTP_GET, .handler = web_server_style_get},
            {.uri = "/settings", .method = HTTP_GET, .handler = web_server_settings_get},
            {.uri = "/settings.js", .method = HTTP_GET, .handler = web_server_settings_js_get},
            {.uri = "/playlist", .method = HTTP_GET, .handler = web_server_playlist_page_get},
            {.uri = "/playlist.js", .method = HTTP_GET, .handler = web_server_playlist_js_get},
            {.uri = "/api/status", .method = HTTP_GET, .handler = web_server_status_get},
            {.uri = "/api/wifi", .method = HTTP_POST, .handler = web_server_wifi_post},
            {.uri = "/api/wifi-scan", .method = HTTP_POST, .handler = web_server_wifi_scan_post},
            {.uri = "/api/wifi-scan", .method = HTTP_GET, .handler = web_server_wifi_scan_get},
            {.uri = "/api/files", .method = HTTP_GET, .handler = web_server_files_get},
            {.uri = "/api/stations", .method = HTTP_GET, .handler = web_server_stations_get},
            {.uri = "/api/playlist", .method = HTTP_GET, .handler = web_server_playlist_get},
            {.uri = "/api/playlist", .method = HTTP_POST, .handler = web_server_playlist_post},
            {.uri = "/api/yandex", .method = HTTP_GET, .handler = web_server_yandex_get},
            {.uri = "/api/yandex", .method = HTTP_POST, .handler = web_server_yandex_post},
            {.uri = "/api/settings", .method = HTTP_GET, .handler = web_server_settings_api_get},
            {.uri = "/api/settings", .method = HTTP_POST, .handler = web_server_settings_api_post},
            {.uri = "/api/station-test", .method = HTTP_POST, .handler = web_server_station_test_post},
            {.uri = "/api/station-icon", .method = HTTP_POST, .handler = web_server_station_icon_post},
            {.uri = "/api/station-icon", .method = HTTP_GET, .handler = web_server_station_icon_get},
            {.uri = "/api/progress", .method = HTTP_GET, .handler = web_server_progress_get},
            {.uri = "/api/cover", .method = HTTP_GET, .handler = web_server_cover_get},
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
