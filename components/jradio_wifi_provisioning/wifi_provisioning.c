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
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#define WIFI_PROVISIONING_RETRIES_PER_NETWORK 3
#define WIFI_RECONNECT_TASK_STACK_SIZE 4096

static const char *TAG = "wifi_mgr";

static bool s_initialized;
static bool s_started;
static bool s_wifi_started;
static bool s_pending_commit;
static uint8_t s_network_index;
static uint8_t s_retry_count;
static wifi_settings_t s_settings;
static wifi_settings_t s_committed_settings;
static wifi_provisioning_status_t s_status = {
    .mode = WIFI_PROVISIONING_AP_SETUP,
};
static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_settings_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_pending_lock = portMUX_INITIALIZER_UNLOCKED;
static QueueHandle_t s_reconnect_queue;
static TaskHandle_t s_reconnect_task;
static bool s_disconnect_pending;
static uint8_t s_disconnect_pending_reason;

#define WIFI_RECONNECT_TASK_POLL_MS 1000

typedef struct {
    enum {
        WIFI_COMMAND_DISCONNECTED = 0,
        WIFI_COMMAND_GOT_IP,
    } type;
    uint8_t reason;
} wifi_disconnect_command_t;

static void secure_zero(void *memory, size_t size)
{
    volatile unsigned char *bytes = memory;
    while (size-- > 0U) {
        *bytes++ = 0U;
    }
}

// Fallback delivery path for WIFI_EVENT_STA_DISCONNECTED: the event handler
// cannot block, so if s_reconnect_queue is momentarily full (the reconnect
// task is busy, e.g. writing to LittleFS) the event would otherwise be lost
// with nothing left to trigger a retry. This single-slot pending flag is
// polled by wifi_reconnect_task so a dropped notification still gets acted
// on, just with bounded extra latency instead of never.
static void mark_disconnect_pending(uint8_t reason)
{
    taskENTER_CRITICAL(&s_pending_lock);
    s_disconnect_pending = true;
    s_disconnect_pending_reason = reason;
    taskEXIT_CRITICAL(&s_pending_lock);
}

static bool take_pending_disconnect(uint8_t *reason)
{
    bool pending;
    taskENTER_CRITICAL(&s_pending_lock);
    pending = s_disconnect_pending;
    if (pending) {
        *reason = s_disconnect_pending_reason;
        s_disconnect_pending = false;
    }
    taskEXIT_CRITICAL(&s_pending_lock);
    return pending;
}

static void status_set_connection(wifi_provisioning_mode_t mode, const char *ssid,
                                  const char *ipv4)
{
    taskENTER_CRITICAL(&s_status_lock);
    s_status.mode = mode;
    snprintf(s_status.active_ssid, sizeof(s_status.active_ssid), "%s", ssid == NULL ? "" : ssid);
    snprintf(s_status.ipv4, sizeof(s_status.ipv4), "%s", ipv4 == NULL ? "" : ipv4);
    taskEXIT_CRITICAL(&s_status_lock);
}

static void status_set_save(bool pending, int32_t error)
{
    taskENTER_CRITICAL(&s_status_lock);
    s_status.save_pending = pending;
    s_status.last_error = error;
    taskEXIT_CRITICAL(&s_status_lock);
}

static void status_set_error(int32_t error)
{
    taskENTER_CRITICAL(&s_status_lock);
    s_status.last_error = error;
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
    status_set_connection(WIFI_PROVISIONING_AP_SETUP, ssid, "192.168.4.1");

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
    wifi_settings_t settings = wifi_provisioning_saved_networks();
    taskENTER_CRITICAL(&s_settings_lock);
    const uint8_t network_index = s_network_index;
    const uint8_t retry_count = s_retry_count;
    taskEXIT_CRITICAL(&s_settings_lock);
    const uint8_t index = wifi_provisioning_network_index(&settings, network_index);
    if (index == WIFI_SETTINGS_MAX_NETWORKS) {
        secure_zero(&settings, sizeof(settings));
        return ESP_ERR_NOT_FOUND;
    }

    const wifi_network_t *network = &settings.networks[index];
    wifi_config_t config = {0};
    const size_t ssid_length = bounded_length(network->ssid, WIFI_SETTINGS_SSID_MAX_LEN);
    const size_t password_length = bounded_length(network->password, WIFI_SETTINGS_PASSWORD_MAX_LEN);
    memcpy(config.sta.ssid, network->ssid, ssid_length);
    memcpy(config.sta.password, network->password, password_length);
    char ssid[WIFI_SETTINGS_SSID_MAX_LEN + 1U];
    snprintf(ssid, sizeof(ssid), "%s", network->ssid);
    status_set_connection(WIFI_PROVISIONING_STA_CONNECTING, ssid, "");
    const esp_err_t config_result = esp_wifi_set_config(WIFI_IF_STA, &config);
    ESP_LOGI(TAG, "connecting ssid=%s network=%u retry=%u password_len=%u", network->ssid,
             (unsigned)index, (unsigned)retry_count, (unsigned)password_length);
    secure_zero(&config, sizeof(config));
    secure_zero(&settings, sizeof(settings));
    if (config_result != ESP_OK) {
        ESP_LOGE(TAG, "configure station failed err=%s", esp_err_to_name(config_result));
    }
    return config_result;
}

static esp_err_t wifi_connect_current_network(void)
{
    ESP_RETURN_ON_ERROR(wifi_configure_current_network(), TAG, "configure station");
    return esp_wifi_connect();
}

static esp_err_t wifi_start_station(void)
{
    taskENTER_CRITICAL(&s_settings_lock);
    s_network_index = 0;
    s_retry_count = 0;
    taskEXIT_CRITICAL(&s_settings_lock);
    ESP_RETURN_ON_ERROR(wifi_stop_if_running(), TAG, "stop Wi-Fi before station");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set station mode");
    ESP_RETURN_ON_ERROR(wifi_configure_current_network(), TAG, "configure first station");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start station");
    s_wifi_started = true;
    return esp_wifi_connect();
}

static bool restore_pending_settings(int32_t error)
{
    bool restored = false;
    taskENTER_CRITICAL(&s_settings_lock);
    if (s_pending_commit) {
        s_settings = s_committed_settings;
        s_pending_commit = false;
        // The reverted list can be shorter than the one s_network_index was
        // chosen against (e.g. it pointed at a newly-added network that no
        // longer exists after the revert); an out-of-range index would make
        // every future reconnect attempt fail lookup with no way to recover.
        if (s_network_index >= s_settings.count) {
            s_network_index = 0;
            s_retry_count = 0;
        }
        restored = true;
    }
    taskEXIT_CRITICAL(&s_settings_lock);
    if (restored) {
        status_set_save(false, error);
    }
    return restored;
}

static void wifi_reconnect_task(void *arg)
{
    (void)arg;
    while (true) {
        wifi_disconnect_command_t command;
        if (xQueueReceive(s_reconnect_queue, &command,
                          pdMS_TO_TICKS(WIFI_RECONNECT_TASK_POLL_MS)) != pdTRUE) {
            uint8_t pending_reason = 0U;
            if (!take_pending_disconnect(&pending_reason)) {
                continue;
            }
            command.type = WIFI_COMMAND_DISCONNECTED;
            command.reason = pending_reason;
        }
        if (command.type == WIFI_COMMAND_GOT_IP) {
            wifi_settings_t committed = {0};
            taskENTER_CRITICAL(&s_settings_lock);
            const bool pending_commit = s_pending_commit;
            if (pending_commit) {
                committed = s_settings;
            }
            taskEXIT_CRITICAL(&s_settings_lock);
            if (pending_commit) {
                const esp_err_t err = wifi_settings_save_atomic(&committed);
                bool completed = false;
                taskENTER_CRITICAL(&s_settings_lock);
                if (s_pending_commit &&
                    memcmp(&s_settings, &committed, sizeof(committed)) == 0) {
                    if (err == ESP_OK) {
                        s_committed_settings = committed;
                    } else {
                        s_settings = s_committed_settings;
                    }
                    s_pending_commit = false;
                    completed = true;
                }
                taskEXIT_CRITICAL(&s_settings_lock);
                if (completed) {
                    status_set_save(false, err == ESP_OK ? 0 : (int32_t)err);
                    if (err == ESP_OK) {
                        ESP_LOGI(TAG, "Wi-Fi settings committed after successful connection");
                    } else {
                        ESP_LOGE(TAG, "failed to commit Wi-Fi settings err=%s",
                                 esp_err_to_name(err));
                    }
                }
            }
            secure_zero(&committed, sizeof(committed));
            continue;
        }
        const wifi_provisioning_status_t status = wifi_provisioning_status();
        if (!wifi_provisioning_should_reconnect(status.mode)) {
            continue;
        }

        status_set_connection(WIFI_PROVISIONING_STA_CONNECTING,
                              status.active_ssid, "");
        status_set_error(command.reason);
        wifi_ap_record_t ap_info = {0};
        const esp_err_t ap_info_err = esp_wifi_sta_get_ap_info(&ap_info);
        if (ap_info_err == ESP_OK) {
            ESP_LOGI(TAG, "AP diagnostics: rssi=%d channel=%u authmode=%u pairwise=%u",
                     ap_info.rssi, (unsigned)ap_info.primary, (unsigned)ap_info.authmode,
                     (unsigned)ap_info.pairwise_cipher);
        } else {
            ESP_LOGI(TAG, "AP diagnostics unavailable err=%s", esp_err_to_name(ap_info_err));
        }
        taskENTER_CRITICAL(&s_settings_lock);
        const bool retry_current = s_retry_count < WIFI_PROVISIONING_RETRIES_PER_NETWORK;
        if (retry_current) {
            ++s_retry_count;
        }
        const uint8_t retry_count = s_retry_count;
        taskEXIT_CRITICAL(&s_settings_lock);
        if (retry_current) {
            ESP_LOGW(TAG, "station disconnected reason=%u retry=%u", (unsigned)command.reason,
                     (unsigned)retry_count);
            const esp_err_t err = wifi_connect_current_network();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "station reconnect failed err=%s", esp_err_to_name(err));
            }
            continue;
        }

        taskENTER_CRITICAL(&s_settings_lock);
        const bool pending_commit = s_pending_commit;
        wifi_settings_t restored = s_committed_settings;
        if (pending_commit) {
            s_settings = restored;
            s_pending_commit = false;
        }
        taskEXIT_CRITICAL(&s_settings_lock);
        if (pending_commit) {
            status_set_save(false, command.reason);
            ESP_LOGW(TAG, "pending Wi-Fi settings rejected; restoring committed settings");
            if (restored.count > 0) {
                (void)wifi_start_station();
            } else {
                (void)wifi_start_ap_setup();
            }
            secure_zero(&restored, sizeof(restored));
            continue;
        }
        secure_zero(&restored, sizeof(restored));

        taskENTER_CRITICAL(&s_settings_lock);
        ++s_network_index;
        s_retry_count = 0;
        const uint8_t network_index = s_network_index;
        taskEXIT_CRITICAL(&s_settings_lock);
        wifi_settings_t settings = wifi_provisioning_saved_networks();
        const bool have_next =
            wifi_provisioning_network_index(&settings, network_index) !=
            WIFI_SETTINGS_MAX_NETWORKS;
        secure_zero(&settings, sizeof(settings));
        if (have_next) {
            ESP_LOGW(TAG, "moving to saved network=%u", (unsigned)network_index);
            const esp_err_t err = wifi_connect_current_network();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "next station connection failed err=%s", esp_err_to_name(err));
            }
            continue;
        }

        ESP_LOGW(TAG, "all saved networks failed; starting setup AP");
        const esp_err_t err = wifi_start_ap_setup();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "setup AP failed err=%s", esp_err_to_name(err));
        }
    }
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
    if (!wifi_provisioning_should_reconnect(status.mode) || s_reconnect_queue == NULL) {
        return;
    }
    wifi_disconnect_command_t command = {
        .type = WIFI_COMMAND_DISCONNECTED,
        .reason = disconnected->reason,
    };
    if (xQueueSend(s_reconnect_queue, &command, 0) != pdPASS) {
        // Queue is momentarily full (reconnect task busy); leave a pending
        // marker so the task's periodic poll still retries this reason
        // instead of the disconnect going unhandled indefinitely.
        mark_disconnect_pending(disconnected->reason);
        ESP_LOGW(TAG, "Wi-Fi reconnect queue full; deferred to periodic poll");
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
    status_set_connection(WIFI_PROVISIONING_STA_CONNECTED,
                          previous.active_ssid, ip);
    taskENTER_CRITICAL(&s_settings_lock);
    s_retry_count = 0;
    const bool pending_commit = s_pending_commit;
    taskEXIT_CRITICAL(&s_settings_lock);
    status_set_save(pending_commit, 0);
    ESP_LOGI(TAG, "connected ssid=%s ip=" IPSTR, previous.active_ssid,
             IP2STR(&got_ip->ip_info.ip));
    if (s_reconnect_queue != NULL && pending_commit) {
        const wifi_disconnect_command_t command = {.type = WIFI_COMMAND_GOT_IP};
        if (xQueueSend(s_reconnect_queue, &command, 0) != pdPASS) {
            ESP_LOGE(TAG, "failed to queue Wi-Fi settings commit");
            (void)restore_pending_settings(ESP_ERR_TIMEOUT);
        }
    }
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
    /* The player is mains-powered and needs predictable receive latency. Modem sleep can
     * leave the small real-time audio pipeline without data between beacon intervals. */
    ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_NONE), TAG, "disable Wi-Fi power save");
    s_reconnect_queue = xQueueCreate(4, sizeof(wifi_disconnect_command_t));
    if (s_reconnect_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(wifi_reconnect_task, "wifi_reconnect", WIFI_RECONNECT_TASK_STACK_SIZE, NULL,
                    tskIDLE_PRIORITY + 2, &s_reconnect_task) != pdPASS) {
        vQueueDelete(s_reconnect_queue);
        s_reconnect_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
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
    s_committed_settings = settings;
    s_pending_commit = false;
    s_network_index = 0;
    s_retry_count = 0;
    taskEXIT_CRITICAL(&s_settings_lock);
    status_set_save(false, 0);
    s_started = true;
    const esp_err_t result =
        wifi_provisioning_initial_mode(&settings) == WIFI_PROVISIONING_AP_SETUP
            ? wifi_start_ap_setup()
            : wifi_start_station();
    secure_zero(&settings, sizeof(settings));
    return result;
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

wifi_provisioning_saved_ssids_t wifi_provisioning_committed_ssids(void)
{
    wifi_settings_t committed;
    taskENTER_CRITICAL(&s_settings_lock);
    committed = s_committed_settings;
    taskEXIT_CRITICAL(&s_settings_lock);
    wifi_provisioning_saved_ssids_t output;
    wifi_provisioning_extract_ssids(&committed, &output);
    secure_zero(&committed, sizeof(committed));
    return output;
}

esp_err_t wifi_provisioning_save_network(const char *ssid, const char *password)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    wifi_settings_t updated = {0};
    wifi_settings_result_t result;
    taskENTER_CRITICAL(&s_settings_lock);
    if (s_pending_commit) {
        taskEXIT_CRITICAL(&s_settings_lock);
        return ESP_ERR_INVALID_STATE;
    }
    updated = s_committed_settings;
    result = wifi_settings_upsert(&updated, ssid, password);
    if (result == WIFI_SETTINGS_OK) {
        s_settings = updated;
        s_pending_commit = true;
        s_network_index = 0;
        for (uint8_t index = 0; index < updated.count; ++index) {
            if (strcmp(updated.networks[index].ssid, ssid) == 0) {
                s_network_index = index;
                break;
            }
        }
        s_retry_count = 0;
    }
    taskEXIT_CRITICAL(&s_settings_lock);
    if (result == WIFI_SETTINGS_INVALID_ARG) {
        secure_zero(&updated, sizeof(updated));
        status_set_save(false, ESP_ERR_INVALID_ARG);
        return ESP_ERR_INVALID_ARG;
    }
    if (result == WIFI_SETTINGS_FULL) {
        secure_zero(&updated, sizeof(updated));
        status_set_save(false, ESP_ERR_NO_MEM);
        return ESP_ERR_NO_MEM;
    }
    status_set_save(true, 0);

    esp_err_t operation = wifi_stop_if_running();
    const bool should_recover = operation == ESP_OK;
    if (operation == ESP_OK) operation = esp_wifi_set_mode(WIFI_MODE_STA);
    if (operation == ESP_OK) operation = wifi_configure_current_network();
    if (operation == ESP_OK) operation = esp_wifi_start();
    if (operation == ESP_OK) {
        s_wifi_started = true;
        operation = esp_wifi_connect();
    }
    if (operation != ESP_OK) {
        (void)restore_pending_settings((int32_t)operation);
        if (should_recover) {
            bool have_committed_networks;
            taskENTER_CRITICAL(&s_settings_lock);
            have_committed_networks = s_committed_settings.count > 0U;
            taskEXIT_CRITICAL(&s_settings_lock);
            const esp_err_t recovery = have_committed_networks
                                           ? wifi_start_station()
                                           : wifi_start_ap_setup();
            if (recovery != ESP_OK) {
                ESP_LOGE(TAG, "restore previous Wi-Fi mode failed err=%s",
                         esp_err_to_name(recovery));
            }
            status_set_save(false, (int32_t)operation);
        }
        ESP_LOGE(TAG, "apply pending Wi-Fi network failed err=%s",
                 esp_err_to_name(operation));
    }
    secure_zero(&updated, sizeof(updated));
    return operation;
}

esp_err_t wifi_provisioning_reset(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(wifi_settings_clear(), TAG, "clear Wi-Fi settings");
    taskENTER_CRITICAL(&s_settings_lock);
    wifi_provisioning_reset_saved_state(&s_settings, &s_committed_settings,
                                        &s_pending_commit);
    s_network_index = 0;
    s_retry_count = 0;
    taskEXIT_CRITICAL(&s_settings_lock);
    status_set_save(false, 0);
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

bool wifi_provisioning_should_reconnect(wifi_provisioning_mode_t mode)
{
    return mode == WIFI_PROVISIONING_STA_CONNECTING || mode == WIFI_PROVISIONING_STA_CONNECTED;
}

void wifi_provisioning_reset_saved_state(wifi_settings_t *current, wifi_settings_t *committed,
                                         bool *pending_commit)
{
    if (current != NULL) {
        *current = (wifi_settings_t){0};
    }
    if (committed != NULL) {
        *committed = (wifi_settings_t){0};
    }
    if (pending_commit != NULL) {
        *pending_commit = false;
    }
}

void wifi_provisioning_extract_ssids(const wifi_settings_t *settings,
                                     wifi_provisioning_saved_ssids_t *output)
{
    if (output == NULL) {
        return;
    }
    *output = (wifi_provisioning_saved_ssids_t){0};
    if (settings == NULL) {
        return;
    }
    output->count = settings->count <= WIFI_SETTINGS_MAX_NETWORKS
                        ? settings->count
                        : WIFI_SETTINGS_MAX_NETWORKS;
    for (uint8_t index = 0; index < output->count; ++index) {
        size_t length = 0U;
        while (length < WIFI_SETTINGS_SSID_MAX_LEN &&
               settings->networks[index].ssid[length] != '\0') {
            output->ssids[index][length] =
                settings->networks[index].ssid[length];
            ++length;
        }
        output->ssids[index][length] = '\0';
    }
}
