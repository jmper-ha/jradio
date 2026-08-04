#pragma once

#include <stdint.h>

#define WIFI_SETTINGS_MAX_NETWORKS 5
#define WIFI_SETTINGS_SSID_MAX_LEN 32
#define WIFI_SETTINGS_PASSWORD_MAX_LEN 64

typedef enum {
    WIFI_SETTINGS_OK = 0,
    WIFI_SETTINGS_INVALID_ARG,
    WIFI_SETTINGS_FULL,
} wifi_settings_result_t;

typedef struct {
    char ssid[WIFI_SETTINGS_SSID_MAX_LEN + 1];
    char password[WIFI_SETTINGS_PASSWORD_MAX_LEN + 1];
} wifi_network_t;

typedef struct {
    wifi_network_t networks[WIFI_SETTINGS_MAX_NETWORKS];
    uint8_t count;
} wifi_settings_t;

wifi_settings_result_t wifi_settings_upsert(wifi_settings_t *settings, const char *ssid,
                                            const char *password);

#ifdef ESP_PLATFORM
#include "esp_err.h"

esp_err_t wifi_settings_storage_init(void);
esp_err_t wifi_settings_load(wifi_settings_t *settings);
esp_err_t wifi_settings_save_atomic(const wifi_settings_t *settings);
esp_err_t wifi_settings_clear(void);
#endif
