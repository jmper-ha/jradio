#pragma once

#include "wifi_settings.h"

typedef enum {
    WEB_SERVER_PARSE_OK = 0,
    WEB_SERVER_PARSE_INVALID,
} web_server_parse_result_t;

web_server_parse_result_t web_server_parse_wifi_request(const char *request, wifi_network_t *network);

#ifdef ESP_PLATFORM
#include "esp_err.h"

esp_err_t web_server_start(void);
esp_err_t web_server_stop(void);
#endif
