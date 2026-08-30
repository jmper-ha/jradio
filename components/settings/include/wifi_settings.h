#pragma once

#include <stdbool.h>
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

/* Removing a network and moving one to the front are the two edits the web page
 * can ask for that no connection attempt can confirm or refuse, so they change
 * the list here and the caller writes the file straight away. Both report
 * whether the SSID was in the list at all.
 *
 * The order of the list is its priority: wifi_provisioning walks it from the
 * top, so "make this one first" is a move to index 0 and needs no field of its
 * own in wifi.json. */
bool wifi_settings_remove(wifi_settings_t *settings, const char *ssid);
bool wifi_settings_promote(wifi_settings_t *settings, const char *ssid);

/* Where the SSID sits in the list, or WIFI_SETTINGS_MAX_NETWORKS when it is not
 * in it. Public because the connection walk is now driven by name: an index
 * kept across attempts would quietly point at a different network the moment
 * the page forgets or reorders one. */
uint8_t wifi_settings_index_of(const wifi_settings_t *settings, const char *ssid);

#ifdef ESP_PLATFORM
#include "esp_err.h"

esp_err_t wifi_settings_storage_init(void);
esp_err_t wifi_settings_load(wifi_settings_t *settings);
esp_err_t wifi_settings_save_atomic(const wifi_settings_t *settings);
esp_err_t wifi_settings_clear(void);
#endif
