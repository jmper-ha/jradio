#pragma once

#include "wifi_settings.h"

typedef enum {
    WEB_SERVER_PARSE_OK = 0,
    WEB_SERVER_PARSE_INVALID,
} web_server_parse_result_t;

web_server_parse_result_t web_server_parse_wifi_request(const char *request, wifi_network_t *network);

/* The Yandex Music link/unlink control. A single "action" field rather than
 * separate endpoints, because all three are the same kind of request against
 * the same piece of state. */
typedef enum {
    WEB_SERVER_YANDEX_ACTION_INVALID = 0,
    WEB_SERVER_YANDEX_ACTION_BEGIN,
    WEB_SERVER_YANDEX_ACTION_CANCEL,
    WEB_SERVER_YANDEX_ACTION_FORGET,
    WEB_SERVER_YANDEX_ACTION_REFRESH,
} web_server_yandex_action_t;

web_server_yandex_action_t web_server_parse_yandex_action(const char *request);

#ifdef ESP_PLATFORM
#include "esp_err.h"

esp_err_t web_server_start(void);
esp_err_t web_server_stop(void);
#endif
