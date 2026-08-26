#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "player_control_types.h"
#include "web_protocol.h"
#include "web_settings.h"
#include "wifi_provisioning.h"

#define WEB_SOCKET_MAX_CLIENTS 4U
// Total httpd session slots: WebSocket clients plus headroom for the several
// concurrent plain HTTP connections a browser opens while loading a page
// (HTML + JS + CSS + fetch requests). With too little headroom here, those
// connections get refused outright once WS clients occupy some of the pool,
// which is what caused pages to load slowly or incompletely.
#define WEB_SOCKET_SERVER_SOCKET_CAPACITY (WEB_SOCKET_MAX_CLIENTS + 6U)

enum {
    WEB_SOCKET_FRAME_CONTINUATION = 0x0,
    WEB_SOCKET_FRAME_TEXT = 0x1,
    WEB_SOCKET_FRAME_BINARY = 0x2,
    WEB_SOCKET_FRAME_CLOSE = 0x8,
    WEB_SOCKET_FRAME_PING = 0x9,
    WEB_SOCKET_FRAME_PONG = 0xA,
};

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WEB_SOCKET_SECTION_NONE = 0U,
    WEB_SOCKET_SECTION_CAPABILITIES = 1U << 0,
    WEB_SOCKET_SECTION_PLAYER = 1U << 1,
    WEB_SOCKET_SECTION_LIST = 1U << 2,
    WEB_SOCKET_SECTION_WIFI = 1U << 3,
    WEB_SOCKET_SECTION_SETTINGS = 1U << 4,
    WEB_SOCKET_SECTION_ALL = WEB_SOCKET_SECTION_CAPABILITIES |
                             WEB_SOCKET_SECTION_PLAYER |
                             WEB_SOCKET_SECTION_LIST |
                             WEB_SOCKET_SECTION_WIFI |
                             WEB_SOCKET_SECTION_SETTINGS,
} web_socket_section_t;

typedef enum {
    WEB_SOCKET_EVENT_SNAPSHOT = 0,
    WEB_SOCKET_EVENT_CAPABILITIES_UPDATE,
    WEB_SOCKET_EVENT_PLAYER_UPDATE,
    WEB_SOCKET_EVENT_LIST_UPDATE,
    WEB_SOCKET_EVENT_WIFI_UPDATE,
    WEB_SOCKET_EVENT_SETTINGS_UPDATE,
} web_socket_event_kind_t;

typedef struct {
    wifi_provisioning_mode_t mode;
    char active_ssid[WIFI_SETTINGS_SSID_MAX_LEN + 1U];
    char ipv4[16];
    int32_t last_error;
    bool save_pending;
    uint8_t saved_count;
    char saved_ssids[WIFI_SETTINGS_MAX_NETWORKS]
                    [WIFI_SETTINGS_SSID_MAX_LEN + 1U];
} web_socket_wifi_state_t;

/* The device settings as they travel to the browser.
 *
 * Pushed rather than polled because the knob and the buttons change them
 * without anything else being told, and settings.csv is eleven reads - far too
 * much to ask on a timer. They are small enough to sit beside the player in
 * the same frame, and they change about as often as the Wi-Fi state does,
 * which is to say almost never.
 *
 * `known` is false until the UI task has published. app_main starts the UI
 * before the web server, so in practice it is true by the time a browser can
 * connect - and one that somehow beats it is better told nothing than told a
 * page full of zeroes. */
typedef struct {
    bool known;
    web_settings_view_t view;
} web_socket_settings_state_t;

typedef const char *(*web_socket_station_label_fn)(size_t index, void *context);

int web_server_command_result(char *output, size_t output_size,
                              const char *request_id, bool ok,
                              const char *error);
int web_socket_serialize_event(char *output, size_t output_size,
                               web_socket_event_kind_t kind, uint32_t revision,
                               const player_snapshot_t *player,
                               const web_socket_wifi_state_t *wifi,
                               const web_socket_settings_state_t *settings,
                               web_socket_station_label_fn station_label,
                               void *station_context);
uint32_t web_socket_changed_sections(
    const player_snapshot_t *previous_player,
    const web_socket_wifi_state_t *previous_wifi,
    const web_socket_settings_state_t *previous_settings,
    const player_snapshot_t *current_player,
    const web_socket_wifi_state_t *current_wifi,
    const web_socket_settings_state_t *current_settings);
uint16_t web_socket_rejection_close_code(bool final, uint8_t frame_type,
                                          size_t payload_length);
uint32_t web_socket_committed_revision(uint32_t current, bool complete);

#ifdef ESP_PLATFORM
#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t web_socket_start(httpd_handle_t server);
esp_err_t web_socket_stop(void);
bool web_socket_queue_wifi(const wifi_network_t *network);
#endif

#ifdef __cplusplus
}
#endif
