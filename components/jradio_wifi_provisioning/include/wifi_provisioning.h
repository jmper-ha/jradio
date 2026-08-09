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
    bool save_pending;
} wifi_provisioning_status_t;

typedef struct {
    uint8_t count;
    char ssids[WIFI_SETTINGS_MAX_NETWORKS][WIFI_SETTINGS_SSID_MAX_LEN + 1];
} wifi_provisioning_saved_ssids_t;

wifi_provisioning_mode_t wifi_provisioning_initial_mode(const wifi_settings_t *settings);
uint8_t wifi_provisioning_network_index(const wifi_settings_t *settings, uint8_t candidate);
bool wifi_provisioning_should_reconnect(wifi_provisioning_mode_t mode);
void wifi_provisioning_reset_saved_state(wifi_settings_t *current, wifi_settings_t *committed,
                                         bool *pending_commit);
void wifi_provisioning_extract_ssids(const wifi_settings_t *settings,
                                     wifi_provisioning_saved_ssids_t *output);

#ifdef ESP_PLATFORM
#include "esp_err.h"

esp_err_t wifi_provisioning_init(void);
esp_err_t wifi_provisioning_start(void);
wifi_provisioning_status_t wifi_provisioning_status(void);
bool wifi_provisioning_get_rssi(int8_t *rssi);
wifi_settings_t wifi_provisioning_saved_networks(void);
wifi_provisioning_saved_ssids_t wifi_provisioning_committed_ssids(void);
esp_err_t wifi_provisioning_save_network(const char *ssid, const char *password);
esp_err_t wifi_provisioning_reset(void);
#endif
