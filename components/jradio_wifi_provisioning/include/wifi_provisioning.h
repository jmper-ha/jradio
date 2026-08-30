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
    /* Per entry of the list above: the user switched this network off from the
     * web page and it is skipped until the next boot. */
    bool blocked[WIFI_SETTINGS_MAX_NETWORKS];
} wifi_provisioning_saved_ssids_t;

/* The networks the "disconnect" button has switched off. Deliberately never
 * written anywhere: the whole meaning of the button is that the credentials
 * stay and a reboot brings the network back. */
typedef struct {
    uint8_t count;
    char ssids[WIFI_SETTINGS_MAX_NETWORKS][WIFI_SETTINGS_SSID_MAX_LEN + 1];
} wifi_blocked_ssids_t;

bool wifi_blocked_contains(const wifi_blocked_ssids_t *blocked, const char *ssid);
bool wifi_blocked_add(wifi_blocked_ssids_t *blocked, const char *ssid);
/* Saving a network again is how the user takes it off the list, so this is the
 * one thing besides a reboot that unblocks one. */
bool wifi_blocked_remove(wifi_blocked_ssids_t *blocked, const char *ssid);

wifi_provisioning_mode_t wifi_provisioning_initial_mode(const wifi_settings_t *settings);

/* The network to try after `after_ssid`, skipping the blocked ones, or
 * WIFI_SETTINGS_MAX_NETWORKS when the list is exhausted - which is the signal
 * to raise the setup AP. A NULL or unknown `after_ssid` starts from the top,
 * so the same call covers both the first attempt and every one after it.
 *
 * Driven by name rather than by an index carried between attempts: forgetting
 * a network or moving one to the front reshuffles the list under a walk that
 * is already in progress, and an index would then land on a different network
 * without anything looking wrong. */
uint8_t wifi_provisioning_next_network(const wifi_settings_t *settings,
                                       const wifi_blocked_ssids_t *blocked,
                                       const char *after_ssid);

bool wifi_provisioning_should_reconnect(wifi_provisioning_mode_t mode);
void wifi_provisioning_reset_saved_state(wifi_settings_t *current, wifi_settings_t *committed,
                                         bool *pending_commit);
void wifi_provisioning_extract_ssids(const wifi_settings_t *settings,
                                     const wifi_blocked_ssids_t *blocked,
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

/* The three edits the settings page can make to a saved network. None of them
 * can be confirmed by a connection attempt the way a new password can, so each
 * writes wifi.json straight away - and each refuses while a save is still
 * waiting for its IP, because that save's rollback would otherwise put the
 * edited-away network back.
 *
 * Forgetting the active network drops the connection with it: staying joined to
 * a network the device no longer remembers is a state nothing else expects. */
esp_err_t wifi_provisioning_forget_network(const char *ssid);
esp_err_t wifi_provisioning_prioritize_network(const char *ssid);
esp_err_t wifi_provisioning_disconnect_active(void);

/* Looking around for networks to join. Offered only while the setup AP is up:
 * a scan hops every channel for a few seconds, which would stall the stream
 * and the page alike, and in that mode there is no stream and nothing else the
 * radio is needed for. esp_wifi_scan_start() is asynchronous, so the caller
 * starts one and polls for the answer - the HTTP server has a single worker
 * task and a handler that waited here would freeze every other request.
 *
 * scan_result() reports ESP_ERR_NOT_FINISHED while one is running and
 * ESP_ERR_INVALID_STATE when none has been started. */
#define WIFI_PROVISIONING_SCAN_MAX 24U

typedef struct {
    char ssid[WIFI_SETTINGS_SSID_MAX_LEN + 1];
    int8_t rssi;
    bool secure;
} wifi_provisioning_scan_entry_t;

esp_err_t wifi_provisioning_scan_start(void);
esp_err_t wifi_provisioning_scan_result(wifi_provisioning_scan_entry_t *entries,
                                        uint8_t capacity, uint8_t *count);

esp_err_t wifi_provisioning_reset(void);
#endif
