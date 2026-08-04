#include "wifi_provisioning.h"

#include <stddef.h>

#ifdef ESP_PLATFORM
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#define WIFI_PROVISIONING_RETRIES_PER_NETWORK 3

static const char *TAG = "wifi_mgr";

static bool s_initialized;
static bool s_started;
static bool s_wifi_started;
static uint8_t s_network_index;
static uint8_t s_retry_count;
static wifi_settings_t s_settings;
static wifi_provisioning_status_t s_status = {
    .mode = WIFI_PROVISIONING_AP_SETUP,
};
static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_settings_lock = portMUX_INITIALIZER_UNLOCKED;

static void status_set(wifi_provisioning_mode_t mode, const char *ssid, const char *ipv4,
                       int32_t last_error)
{
    taskENTER_CRITICAL(&s_status_lock);
    s_status.mode = mode;
    snprintf(s_status.active_ssid, sizeof(s_status.active_ssid), "%s", ssid == NULL ? "" : ssid);
    snprintf(s_status.ipv4, sizeof(s_status.ipv4), "%s", ipv4 == NULL ? "" : ipv4);
    s_status.last_error = last_error;
    taskEXIT_CRITICAL(&s_status_lock);
}

static size_t bounded_length(const char *value, size_t maximum)
{
    size_t length = 0;
    while (length < maximum && value[length] != '\0') {
        ++length;
    }
    return length;
}

static esp_err_t wifi_stop_if_running(void)
{
    if (!s_wifi_started) {
        return ESP_OK;
    }
    const esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) {
        return err;
    }
    s_wifi_started = false;
    return ESP_OK;
}

static esp_err_t wifi_configure_static_ap_ip(void)
{
    const esp_netif_ip_info_t ip_info = {
        .ip.addr = ESP_IP4TOADDR(192, 168, 4, 1),
        .gw.addr = ESP_IP4TOADDR(192, 168, 4, 1),
        .netmask.addr = ESP_IP4TOADDR(255, 255, 255, 0),
    };
    esp_err_t err = esp_netif_dhcps_stop(s_ap_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        return err;
    }
    ESP_RETURN_ON_ERROR(esp_netif_set_ip_info(s_ap_netif, &ip_info), TAG, "set AP address");
    err = esp_netif_dhcps_start(s_ap_netif);
    return err == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED ? ESP_OK : err;
}

static esp_err_t wifi_start_ap_setup(void)
{
    uint8_t mac[6] = {0};
    ESP_RETURN_ON_ERROR(esp_read_mac(mac, ESP_MAC_WIFI_STA), TAG, "read Wi-Fi MAC");

    char ssid[WIFI_SETTINGS_SSID_MAX_LEN + 1];
    snprintf(ssid, sizeof(ssid), "jradio-%02X%02X", mac[4], mac[5]);
    status_set(WIFI_PROVISIONING_AP_SETUP, ssid, "192.168.4.1", 0);

    ESP_RETURN_ON_ERROR(wifi_stop_if_running(), TAG, "stop Wi-Fi before AP");
    ESP_RETURN_ON_ERROR(wifi_configure_static_ap_ip(), TAG, "configure AP address");

    wifi_config_t config = {
        .ap = {
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
        },
    };
    memcpy(config.ap.ssid, ssid, strlen(ssid));
    config.ap.ssid_len = strlen(ssid);
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "set AP mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &config), TAG, "configure AP");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start AP");
    s_wifi_started = true;
    ESP_LOGI(TAG, "AP setup started ssid=%s ip=192.168.4.1", ssid);
    return ESP_OK;
}

static esp_err_t wifi_configure_current_network(void)
{
    const wifi_settings_t settings = wifi_provisioning_saved_networks();
    const uint8_t index = wifi_provisioning_network_index(&settings, s_network_index);
    if (index == WIFI_SETTINGS_MAX_NETWORKS) {
        return ESP_ERR_NOT_FOUND;
    }

    const wifi_network_t *network = &settings.networks[index];
    wifi_config_t config = {0};
    const size_t ssid_length = bounded_length(network->ssid, WIFI_SETTINGS_SSID_MAX_LEN);
    const size_t password_length = bounded_length(network->password, WIFI_SETTINGS_PASSWORD_MAX_LEN);
    memcpy(config.sta.ssid, network->ssid, ssid_length);
    memcpy(config.sta.password, network->password, password_length);
    status_set(WIFI_PROVISIONING_STA_CONNECTING, network->ssid, "", 0);
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &config), TAG, "configure station");
    ESP_LOGI(TAG, "connecting ssid=%s network=%u retry=%u", network->ssid, (unsigned)index,
             (unsigned)s_retry_count);
    return ESP_OK;
}

static esp_err_t wifi_connect_current_network(void)
{
    ESP_RETURN_ON_ERROR(wifi_configure_current_network(), TAG, "configure station");
    return esp_wifi_connect();
}

static esp_err_t wifi_start_station(void)
{
    s_network_index = 0;
    s_retry_count = 0;
    ESP_RETURN_ON_ERROR(wifi_stop_if_running(), TAG, "stop Wi-Fi before station");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set station mode");
    ESP_RETURN_ON_ERROR(wifi_configure_current_network(), TAG, "configure first station");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start station");
    s_wifi_started = true;
    return esp_wifi_connect();
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                               void *event_data)
{
    (void)arg;
    if (event_base != WIFI_EVENT || event_id != WIFI_EVENT_STA_DISCONNECTED || !s_started) {
        return;
    }
    const wifi_event_sta_disconnected_t *disconnected = event_data;
    const wifi_provisioning_status_t status = wifi_provisioning_status();
    if (status.mode != WIFI_PROVISIONING_STA_CONNECTING &&
        status.mode != WIFI_PROVISIONING_STA_CONNECTED) {
        return;
    }

    status_set(WIFI_PROVISIONING_STA_CONNECTING, status.active_ssid, "", disconnected->reason);
    if (s_retry_count < WIFI_PROVISIONING_RETRIES_PER_NETWORK) {
        ++s_retry_count;
        ESP_LOGW(TAG, "station disconnected reason=%u retry=%u", (unsigned)disconnected->reason,
                 (unsigned)s_retry_count);
        const esp_err_t err = wifi_connect_current_network();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "station reconnect failed err=%s", esp_err_to_name(err));
        }
        return;
    }

    ++s_network_index;
    s_retry_count = 0;
    const wifi_settings_t settings = wifi_provisioning_saved_networks();
    if (wifi_provisioning_network_index(&settings, s_network_index) !=
        WIFI_SETTINGS_MAX_NETWORKS) {
        ESP_LOGW(TAG, "moving to saved network=%u", (unsigned)s_network_index);
        const esp_err_t err = wifi_connect_current_network();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "next station connection failed err=%s", esp_err_to_name(err));
        }
        return;
    }

    ESP_LOGW(TAG, "all saved networks failed; starting setup AP");
    const esp_err_t err = wifi_start_ap_setup();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "setup AP failed err=%s", esp_err_to_name(err));
    }
}

static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                             void *event_data)
{
    (void)arg;
    if (event_base != IP_EVENT || event_id != IP_EVENT_STA_GOT_IP || !s_started) {
        return;
    }
    const ip_event_got_ip_t *got_ip = event_data;
    const wifi_provisioning_status_t previous = wifi_provisioning_status();
    char ip[16];
    snprintf(ip, sizeof(ip), IPSTR, IP2STR(&got_ip->ip_info.ip));
    status_set(WIFI_PROVISIONING_STA_CONNECTED, previous.active_ssid, ip, 0);
    s_retry_count = 0;
    ESP_LOGI(TAG, "connected ssid=%s ip=" IPSTR, previous.active_ssid,
             IP2STR(&got_ip->ip_info.ip));
}

static esp_err_t wifi_init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase NVS");
        err = nvs_flash_init();
    }
    return err;
}

static esp_err_t wifi_init_once(esp_err_t (*init_fn)(void))
{
    const esp_err_t err = init_fn();
    return err == ESP_ERR_INVALID_STATE ? ESP_OK : err;
}

esp_err_t wifi_provisioning_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(wifi_init_nvs(), TAG, "initialize NVS");
    ESP_RETURN_ON_ERROR(wifi_init_once(esp_netif_init), TAG, "initialize netif");
    ESP_RETURN_ON_ERROR(wifi_init_once(esp_event_loop_create_default), TAG, "create event loop");

    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (s_sta_netif == NULL || s_ap_netif == NULL) {
        return ESP_ERR_NO_MEM;
    }
    const wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&config), TAG, "initialize Wi-Fi");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "select RAM Wi-Fi storage");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                                    &wifi_event_handler, NULL),
                        TAG, "register Wi-Fi event handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler,
                                                    NULL),
                        TAG, "register IP event handler");
    s_initialized = true;
    return ESP_OK;
}

esp_err_t wifi_provisioning_start(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(wifi_settings_storage_init(), TAG, "mount LittleFS");
    wifi_settings_t settings;
    ESP_RETURN_ON_ERROR(wifi_settings_load(&settings), TAG, "load Wi-Fi settings");
    taskENTER_CRITICAL(&s_settings_lock);
    s_settings = settings;
    taskEXIT_CRITICAL(&s_settings_lock);
    s_started = true;
    return wifi_provisioning_initial_mode(&settings) == WIFI_PROVISIONING_AP_SETUP
               ? wifi_start_ap_setup()
               : wifi_start_station();
}

wifi_provisioning_status_t wifi_provisioning_status(void)
{
    wifi_provisioning_status_t status;
    taskENTER_CRITICAL(&s_status_lock);
    status = s_status;
    taskEXIT_CRITICAL(&s_status_lock);
    return status;
}

bool wifi_provisioning_get_rssi(int8_t *rssi)
{
    if (rssi == NULL || !s_started) {
        return false;
    }
    wifi_ap_record_t ap_info = {0};
    if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) {
        return false;
    }
    *rssi = ap_info.rssi;
    return true;
}

wifi_settings_t wifi_provisioning_saved_networks(void)
{
    wifi_settings_t settings;
    taskENTER_CRITICAL(&s_settings_lock);
    settings = s_settings;
    taskEXIT_CRITICAL(&s_settings_lock);
    return settings;
}

esp_err_t wifi_provisioning_save_network(const char *ssid, const char *password)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    wifi_settings_t updated = wifi_provisioning_saved_networks();
    const wifi_settings_result_t result = wifi_settings_upsert(&updated, ssid, password);
    if (result == WIFI_SETTINGS_INVALID_ARG) {
        return ESP_ERR_INVALID_ARG;
    }
    if (result == WIFI_SETTINGS_FULL) {
        return ESP_ERR_NO_MEM;
    }
    ESP_RETURN_ON_ERROR(wifi_settings_save_atomic(&updated), TAG, "save Wi-Fi settings");
    taskENTER_CRITICAL(&s_settings_lock);
    s_settings = updated;
    taskEXIT_CRITICAL(&s_settings_lock);
    return ESP_OK;
}

esp_err_t wifi_provisioning_reset(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(wifi_settings_clear(), TAG, "clear Wi-Fi settings");
    taskENTER_CRITICAL(&s_settings_lock);
    s_settings = (wifi_settings_t){0};
    taskEXIT_CRITICAL(&s_settings_lock);
    s_network_index = 0;
    s_retry_count = 0;
    s_started = true;
    ESP_LOGI(TAG, "Wi-Fi settings cleared; returning to setup AP");
    return wifi_start_ap_setup();
}
#endif

wifi_provisioning_mode_t wifi_provisioning_initial_mode(const wifi_settings_t *settings)
{
    return settings == NULL || settings->count == 0 ? WIFI_PROVISIONING_AP_SETUP
                                                     : WIFI_PROVISIONING_STA_CONNECTING;
}

uint8_t wifi_provisioning_network_index(const wifi_settings_t *settings, uint8_t candidate)
{
    if (settings == NULL || candidate >= settings->count ||
        candidate >= WIFI_SETTINGS_MAX_NETWORKS) {
        return WIFI_SETTINGS_MAX_NETWORKS;
    }
    return candidate;
}
