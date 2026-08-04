#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "wifi_settings.h"

typedef enum {
    WIFI_PROVISIONING_AP_SETUP = 0,
    WIFI_PROVISIONING_STA_CONNECTING,
    WIFI_PROVISIONING_STA_CONNECTED,
} wifi_provisioning_mode_t;

typedef struct {
    wifi_provisioning_mode_t mode;
    char active_ssid[WIFI_SETTINGS_SSID_MAX_LEN + 1];
    char ipv4[16];
    int32_t last_error;
} wifi_provisioning_status_t;

wifi_provisioning_mode_t wifi_provisioning_initial_mode(const wifi_settings_t *settings);
uint8_t wifi_provisioning_network_index(const wifi_settings_t *settings, uint8_t candidate);

#ifdef ESP_PLATFORM
#include "esp_err.h"

esp_err_t wifi_provisioning_init(void);
esp_err_t wifi_provisioning_start(void);
wifi_provisioning_status_t wifi_provisioning_status(void);
bool wifi_provisioning_get_rssi(int8_t *rssi);
wifi_settings_t wifi_provisioning_saved_networks(void);
esp_err_t wifi_provisioning_save_network(const char *ssid, const char *password);
esp_err_t wifi_provisioning_reset(void);
#endif
