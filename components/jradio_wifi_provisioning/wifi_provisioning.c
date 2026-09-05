#include "wifi_provisioning.h"

#include <stddef.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include <stdio.h>
#include <stdlib.h>

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
/* A scan of every channel takes a few seconds; well past that it is not slow,
 * it is stuck, and the page needs an answer rather than another "wait". */
#define WIFI_SCAN_TIMEOUT_MS 15000
/* Weaker than this and the network is not worth offering: it may associate and
 * will not carry a stream, and a list padded with them buries the two or three
 * networks the user actually has. Applied per sighting, before the strongest of
 * each name is kept, so a network heard once faintly and once well still
 * counts as the good one. */
#define WIFI_SCAN_MIN_RSSI (-80)

static const char *TAG = "wifi_mgr";

/* Where the last working access point of each network was found. Scanning
 * every channel and walking the answers is what makes joining reliable, and it
 * costs ten seconds on a network whose broken access point is the louder one -
 * every boot, because the sort puts it first every time. The address of the
 * one that worked turns that back into about a second, and is worth keeping
 * across a reboot for exactly that reason.
 *
 * Kept in RAM and written through to NVS, so the connect path never reads
 * flash. An entry survives the network being deleted; it is only ever looked
 * up by name, so a stale one is never consulted. */
#define WIFI_AP_HINT_NAMESPACE "jradio_wifi"
#define WIFI_AP_HINT_KEY "ap_hints"

typedef struct {
    char ssid[WIFI_SETTINGS_SSID_MAX_LEN + 1U];
    uint8_t bssid[6];
    uint8_t channel;
} wifi_ap_hint_t;

static wifi_ap_hint_t s_ap_hints[WIFI_SETTINGS_MAX_NETWORKS];
/* Filled when the association succeeds, promoted to the table only once an
 * address has arrived: an access point that completes the handshake and then
 * has no lease to give is not one worth going back to first. */
static wifi_ap_hint_t s_ap_hint_seen;
static bool s_ap_hint_confirmed;
static portMUX_TYPE s_ap_hint_lock = portMUX_INITIALIZER_UNLOCKED;

static bool s_initialized;
static bool s_started;
static bool s_wifi_started;
static bool s_pending_commit;
/* The network the current attempt belongs to, by name. */
static char s_current_ssid[WIFI_SETTINGS_SSID_MAX_LEN + 1U];
static wifi_blocked_ssids_t s_blocked;
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

typedef enum {
    WIFI_SCAN_IDLE = 0,
    WIFI_SCAN_RUNNING,
    WIFI_SCAN_DONE,
} wifi_scan_state_t;

static wifi_scan_state_t s_scan_state;
static TickType_t s_scan_started_tick;
static portMUX_TYPE s_scan_lock = portMUX_INITIALIZER_UNLOCKED;

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

static void wifi_ap_hints_load(void)
{
    nvs_handle_t handle;
    if (nvs_open(WIFI_AP_HINT_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    size_t size = sizeof(s_ap_hints);
    /* A blob of any other length was written by a different layout of this
     * struct; discarding it costs one slow connect and is the whole of the
     * versioning this needs. */
    if (nvs_get_blob(handle, WIFI_AP_HINT_KEY, s_ap_hints, &size) != ESP_OK ||
        size != sizeof(s_ap_hints)) {
        memset(s_ap_hints, 0, sizeof(s_ap_hints));
    }
    nvs_close(handle);
}

static void wifi_ap_hints_save(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_AP_HINT_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "cannot store the access point hint err=%s", esp_err_to_name(err));
        return;
    }
    err = nvs_set_blob(handle, WIFI_AP_HINT_KEY, s_ap_hints, sizeof(s_ap_hints));
    if (err == ESP_OK) err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "cannot store the access point hint err=%s", esp_err_to_name(err));
    }
    nvs_close(handle);
}

static bool wifi_ap_hint_lookup(const char *ssid, wifi_ap_hint_t *hint)
{
    for (size_t index = 0U; index < WIFI_SETTINGS_MAX_NETWORKS; ++index) {
        if (s_ap_hints[index].ssid[0] != '\0' &&
            strncmp(s_ap_hints[index].ssid, ssid, WIFI_SETTINGS_SSID_MAX_LEN) == 0) {
            *hint = s_ap_hints[index];
            return true;
        }
    }
    return false;
}

/* Called from the reconnect task, so it may touch flash. Writes only when the
 * access point actually changed - a reboot onto the same one must not cost an
 * erase cycle. */
static void wifi_ap_hint_flush(void)
{
    wifi_ap_hint_t hint = {0};
    taskENTER_CRITICAL(&s_ap_hint_lock);
    const bool confirmed = s_ap_hint_confirmed;
    if (confirmed) {
        hint = s_ap_hint_seen;
        s_ap_hint_confirmed = false;
    }
    taskEXIT_CRITICAL(&s_ap_hint_lock);
    if (!confirmed || hint.ssid[0] == '\0') return;

    size_t slot = WIFI_SETTINGS_MAX_NETWORKS;
    for (size_t index = 0U; index < WIFI_SETTINGS_MAX_NETWORKS; ++index) {
        if (strncmp(s_ap_hints[index].ssid, hint.ssid, WIFI_SETTINGS_SSID_MAX_LEN) == 0) {
            slot = index;
            break;
        }
        if (s_ap_hints[index].ssid[0] == '\0' && slot == WIFI_SETTINGS_MAX_NETWORKS) {
            slot = index;
        }
    }
    /* The table is as long as the list of networks, so a full one means every
     * slot belongs to a network that was deleted. Any of them will do. */
    if (slot == WIFI_SETTINGS_MAX_NETWORKS) slot = 0U;
    if (memcmp(&s_ap_hints[slot], &hint, sizeof(hint)) == 0) return;
    s_ap_hints[slot] = hint;
    wifi_ap_hints_save();
    ESP_LOGI(TAG, "remembered " MACSTR " channel=%u for ssid=%s", MAC2STR(hint.bssid),
             (unsigned)hint.channel, hint.ssid);
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

/* snprintf inside a critical section is not worth the code it drags in, and
 * every SSID here is already known to fit. */
static void copy_ssid(char *destination, const char *source)
{
    size_t length = 0U;
    while (source != NULL && length < WIFI_SETTINGS_SSID_MAX_LEN && source[length] != '\0') {
        destination[length] = source[length];
        ++length;
    }
    destination[length] = '\0';
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
    /* APSTA rather than AP: esp_wifi_scan_start() refuses without a station
     * interface, and the setup page's list of surrounding networks is the only
     * reason the station side is up here. Nothing connects with it - scanning
     * is offered only in this mode, where there is no stream to interrupt. */
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "set AP mode");
    /* And the station half is emptied on the way in: it still holds the
     * credentials of the network that was just left, and nothing here is going
     * to connect with them. The station side exists only so that
     * esp_wifi_scan_start() has an interface to work on. */
    wifi_config_t station = {0};
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &station), TAG, "clear station");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &config), TAG, "configure AP");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start AP");
    s_wifi_started = true;
    ESP_LOGI(TAG, "AP setup started ssid=%s ip=192.168.4.1", ssid);
    return ESP_OK;
}

/* Makes the next network to try the current one and reports whether there was
 * one. False is the caller's cue to raise the setup AP: either the list is
 * empty or everything left in it is switched off until the next boot. */
static bool wifi_select_next_network(bool from_start)
{
    wifi_settings_t settings;
    bool selected = false;
    taskENTER_CRITICAL(&s_settings_lock);
    settings = s_settings;
    const uint8_t index = wifi_provisioning_next_network(
        &settings, &s_blocked, from_start ? NULL : s_current_ssid);
    if (index != WIFI_SETTINGS_MAX_NETWORKS) {
        copy_ssid(s_current_ssid, settings.networks[index].ssid);
        selected = true;
    } else {
        s_current_ssid[0] = '\0';
    }
    s_retry_count = 0;
    taskEXIT_CRITICAL(&s_settings_lock);
    secure_zero(&settings, sizeof(settings));
    return selected;
}

static esp_err_t wifi_configure_current_network(void)
{
    wifi_settings_t settings = wifi_provisioning_saved_networks();
    char current[WIFI_SETTINGS_SSID_MAX_LEN + 1U];
    taskENTER_CRITICAL(&s_settings_lock);
    copy_ssid(current, s_current_ssid);
    const uint8_t retry_count = s_retry_count;
    taskEXIT_CRITICAL(&s_settings_lock);
    const uint8_t index = wifi_settings_index_of(&settings, current);
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
    /* One name can stand for several access points, and they do not all work.
     * A default connect is a fast scan: it walks the channels from the first
     * and stops at the first answer to the name, which on a flash with no
     * stored last-connection is whichever access point happens to sit on the
     * low channels. Seen on 2026-09-05: a repeater on channel 1 associated and
     * then let the WPA2 handshake time out (reason 15, which reads exactly
     * like a wrong password) while the router on channel 8 took the same
     * password without complaint. Scanning every channel and sorting by signal
     * costs about two seconds per connect and picks the one that can be heard.
     *
     * failure_retry_cnt is the other half and only works with the full scan:
     * without it the driver reports the failure and we start over from a fresh
     * config, which erases its memory of what just refused us - so every retry
     * repeated the first one and the radio never reached the other access
     * point at all.
     *
     * One retry, not three. The bad access point is the *stronger* of the two
     * here, so sorting by signal offers it first at every boot and whatever is
     * spent on it is spent every time: measured 2026-09-05, three retries cost
     * four attempts of three seconds each and reached the router at 16 s, one
     * retry reaches it at 7 s. A network that really is just flaky loses
     * nothing - it comes round again on the next attempt. */
    /* Unless we already know where this network answered last time. Naming the
     * access point and its channel skips the scan and the walk entirely - a
     * second instead of ten - and the first attempt is the only one that gets
     * to try it: if it is wrong, whatever it cost is not spent twice, and the
     * retry below falls back to the search that always works. */
    wifi_ap_hint_t hint;
    const bool pinned = retry_count == 0U && wifi_ap_hint_lookup(network->ssid, &hint);
    if (pinned) {
        config.sta.bssid_set = true;
        memcpy(config.sta.bssid, hint.bssid, sizeof(config.sta.bssid));
        config.sta.channel = hint.channel;
    } else {
        config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
        config.sta.failure_retry_cnt = 1;
    }
    char ssid[WIFI_SETTINGS_SSID_MAX_LEN + 1U];
    snprintf(ssid, sizeof(ssid), "%s", network->ssid);
    status_set_connection(WIFI_PROVISIONING_STA_CONNECTING, ssid, "");
    const esp_err_t config_result = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (pinned) {
        ESP_LOGI(TAG, "connecting ssid=%s network=%u retry=%u password_len=%u via " MACSTR
                      " channel=%u",
                 network->ssid, (unsigned)index, (unsigned)retry_count,
                 (unsigned)password_length, MAC2STR(hint.bssid), (unsigned)hint.channel);
    } else {
        ESP_LOGI(TAG, "connecting ssid=%s network=%u retry=%u password_len=%u scanning",
                 network->ssid, (unsigned)index, (unsigned)retry_count,
                 (unsigned)password_length);
    }
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

static esp_err_t wifi_start_current_station(void)
{
    ESP_RETURN_ON_ERROR(wifi_stop_if_running(), TAG, "stop Wi-Fi before station");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set station mode");
    ESP_RETURN_ON_ERROR(wifi_configure_current_network(), TAG, "configure first station");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start station");
    s_wifi_started = true;
    return esp_wifi_connect();
}

/* The two ways the radio is put back to work, and both end at the setup AP
 * when nothing is left to try: from the top of the list, and one past the
 * network that has just been given up on. */
static esp_err_t wifi_restart_from_top(void)
{
    return wifi_select_next_network(true) ? wifi_start_current_station()
                                          : wifi_start_ap_setup();
}

static esp_err_t wifi_advance_to_next_network(void)
{
    return wifi_select_next_network(false) ? wifi_start_current_station()
                                           : wifi_start_ap_setup();
}

static bool restore_pending_settings(int32_t error)
{
    bool restored = false;
    taskENTER_CRITICAL(&s_settings_lock);
    if (s_pending_commit) {
        s_settings = s_committed_settings;
        s_pending_commit = false;
        // The reverted list no longer holds the network the attempt was for -
        // it was the one being added. Forget the name so the next selection
        // starts from the top instead of looking for something that is gone.
        if (wifi_settings_index_of(&s_settings, s_current_ssid) == WIFI_SETTINGS_MAX_NETWORKS) {
            s_current_ssid[0] = '\0';
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
        wifi_ap_hint_flush();
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
        /* This is where the access point used to be described, from
         * esp_wifi_sta_get_ap_info(). It never once answered: by the time a
         * disconnect has reached this task there is no connection left to ask
         * about, so the line only ever printed "unavailable" and buried the
         * reason it was printed next to. The event handler logs the address
         * and signal instead, from the event, where they actually are. */
        taskENTER_CRITICAL(&s_settings_lock);
        const bool retry_current = s_retry_count < WIFI_PROVISIONING_RETRIES_PER_NETWORK;
        if (retry_current) {
            ++s_retry_count;
        }
        const uint8_t retry_count = s_retry_count;
        taskEXIT_CRITICAL(&s_settings_lock);
        if (retry_current) {
            ESP_LOGW(TAG, "retrying after reason=%u retry=%u/%u", (unsigned)command.reason,
                     (unsigned)retry_count, (unsigned)WIFI_PROVISIONING_RETRIES_PER_NETWORK);
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
            // The reverted list no longer holds the network that was being
            // added, so the walk has to start over rather than continue past a
            // name that is gone.
            taskENTER_CRITICAL(&s_settings_lock);
            s_current_ssid[0] = '\0';
            taskEXIT_CRITICAL(&s_settings_lock);
            (void)wifi_restart_from_top();
            secure_zero(&restored, sizeof(restored));
            continue;
        }
        secure_zero(&restored, sizeof(restored));

        ESP_LOGW(TAG, "network gave up; moving on");
        const esp_err_t err = wifi_advance_to_next_network();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "moving to the next network failed err=%s", esp_err_to_name(err));
        }
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                               void *event_data)
{
    (void)arg;
    if (event_base != WIFI_EVENT || !s_started) return;
    if (event_id == WIFI_EVENT_STA_CONNECTED) {
        /* Held rather than stored: the address it names is only worth going
         * back to first once a lease has proved the network behind it works. */
        const wifi_event_sta_connected_t *connected = event_data;
        wifi_ap_hint_t hint = {0};
        const size_t ssid_length = connected->ssid_len > WIFI_SETTINGS_SSID_MAX_LEN
                                       ? WIFI_SETTINGS_SSID_MAX_LEN
                                       : connected->ssid_len;
        memcpy(hint.ssid, connected->ssid, ssid_length);
        memcpy(hint.bssid, connected->bssid, sizeof(hint.bssid));
        hint.channel = connected->channel;
        taskENTER_CRITICAL(&s_ap_hint_lock);
        s_ap_hint_seen = hint;
        taskEXIT_CRITICAL(&s_ap_hint_lock);
        return;
    }
    if (event_id != WIFI_EVENT_STA_DISCONNECTED) return;
    const wifi_event_sta_disconnected_t *disconnected = event_data;
    /* Which access point refused, not just why. A reason on its own cannot be
     * read: reason 15 is the handshake timing out, and that is what a wrong
     * password looks like *and* what one bad access point of a multi-AP
     * network looks like. The address separates them at a glance. It has to be
     * taken here because it exists nowhere else - esp_wifi_sta_get_ap_info()
     * needs a connection, which by now is exactly what is missing. */
    ESP_LOGW(TAG, "station disconnected from " MACSTR " reason=%u rssi=%d",
             MAC2STR(disconnected->bssid), (unsigned)disconnected->reason,
             (int)disconnected->rssi);
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

static void scan_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                               void *event_data)
{
    (void)arg;
    (void)event_data;
    if (event_base != WIFI_EVENT || event_id != WIFI_EVENT_SCAN_DONE) {
        return;
    }
    taskENTER_CRITICAL(&s_scan_lock);
    if (s_scan_state == WIFI_SCAN_RUNNING) {
        s_scan_state = WIFI_SCAN_DONE;
    }
    taskEXIT_CRITICAL(&s_scan_lock);
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
    taskENTER_CRITICAL(&s_ap_hint_lock);
    s_ap_hint_confirmed = s_ap_hint_seen.ssid[0] != '\0';
    taskEXIT_CRITICAL(&s_ap_hint_lock);
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
    wifi_ap_hints_load();
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
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED,
                                                  &wifi_event_handler, NULL),
                        TAG, "register connect handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                                    &wifi_event_handler, NULL),
                        TAG, "register Wi-Fi event handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE,
                                                    &scan_event_handler, NULL),
                        TAG, "register scan event handler");
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
    s_blocked = (wifi_blocked_ssids_t){0};
    s_current_ssid[0] = '\0';
    s_retry_count = 0;
    taskEXIT_CRITICAL(&s_settings_lock);
    status_set_save(false, 0);
    s_started = true;
    const esp_err_t result =
        wifi_provisioning_initial_mode(&settings) == WIFI_PROVISIONING_AP_SETUP
            ? wifi_start_ap_setup()
            : wifi_restart_from_top();
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
    /* Asking a station that is not joined to anything is not free. While the
     * setup AP is up the station interface exists - it is what scanning needs -
     * and the driver answers every such question with "Haven't to connect to a
     * suitable AP now!". This is asked once a second, which filled the log with
     * one warning a second for as long as the device had no network. */
    if (wifi_provisioning_status().mode != WIFI_PROVISIONING_STA_CONNECTED) {
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
    taskENTER_CRITICAL(&s_settings_lock);
    const wifi_blocked_ssids_t blocked = s_blocked;
    taskEXIT_CRITICAL(&s_settings_lock);
    wifi_provisioning_extract_ssids(&committed, &blocked, &output);
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
        /* Saving a network is also how the user takes it off the switched-off
         * list: asking to connect to it is the opposite of the button that put
         * it there. */
        (void)wifi_blocked_remove(&s_blocked, ssid);
        copy_ssid(s_current_ssid, ssid);
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
            const esp_err_t recovery = wifi_restart_from_top();
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

/* The three edits the settings page can make to the saved list. They share one
 * shape: read the committed list, change the copy, write the file, and only
 * then let the change into the state the reconnect task reads. Writing first
 * means a failed write leaves nothing behind. */
typedef enum {
    WIFI_LIST_EDIT_FORGET = 0,
    WIFI_LIST_EDIT_PROMOTE,
} wifi_list_edit_t;

static esp_err_t wifi_apply_list_edit(wifi_list_edit_t edit, const char *ssid)
{
    if (!s_initialized || ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }
    wifi_settings_t updated;
    taskENTER_CRITICAL(&s_settings_lock);
    const bool busy = s_pending_commit;
    updated = s_committed_settings;
    taskEXIT_CRITICAL(&s_settings_lock);
    /* A save that has not yet earned its IP would be rolled back onto the
     * committed list, putting a network this edit removed straight back. */
    if (busy) {
        secure_zero(&updated, sizeof(updated));
        return ESP_ERR_INVALID_STATE;
    }
    const bool changed = edit == WIFI_LIST_EDIT_FORGET ? wifi_settings_remove(&updated, ssid)
                                                       : wifi_settings_promote(&updated, ssid);
    if (!changed) {
        secure_zero(&updated, sizeof(updated));
        return ESP_ERR_NOT_FOUND;
    }
    const esp_err_t err = wifi_settings_save_atomic(&updated);
    if (err == ESP_OK) {
        taskENTER_CRITICAL(&s_settings_lock);
        if (!s_pending_commit) {
            s_settings = updated;
            s_committed_settings = updated;
        }
        if (edit == WIFI_LIST_EDIT_FORGET) {
            (void)wifi_blocked_remove(&s_blocked, ssid);
        }
        taskEXIT_CRITICAL(&s_settings_lock);
    }
    secure_zero(&updated, sizeof(updated));
    return err;
}

esp_err_t wifi_provisioning_forget_network(const char *ssid)
{
    const wifi_provisioning_status_t status = wifi_provisioning_status();
    const bool was_active = ssid != NULL && strcmp(status.active_ssid, ssid) == 0 &&
                            wifi_provisioning_should_reconnect(status.mode);
    const esp_err_t err = wifi_apply_list_edit(WIFI_LIST_EDIT_FORGET, ssid);
    if (err != ESP_OK) {
        return err;
    }
    ESP_LOGI(TAG, "saved network forgotten (active=%d)", (int)was_active);
    if (!was_active) {
        return ESP_OK;
    }
    /* Staying joined to a network the device no longer remembers is a state
     * nothing else expects, so the connection goes with the credentials. The
     * name is gone from the list, so the walk starts over. */
    taskENTER_CRITICAL(&s_settings_lock);
    s_current_ssid[0] = '\0';
    taskEXIT_CRITICAL(&s_settings_lock);
    return wifi_restart_from_top();
}

esp_err_t wifi_provisioning_prioritize_network(const char *ssid)
{
    /* Deliberately no reconnection: the order decides which network the next
     * attempt starts from, and dropping a working stream to prove it would be
     * a surprising price for a checkbox. */
    const esp_err_t err = wifi_apply_list_edit(WIFI_LIST_EDIT_PROMOTE, ssid);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "saved network moved to the front of the list");
    }
    return err;
}

esp_err_t wifi_provisioning_disconnect_active(void)
{
    const wifi_provisioning_status_t status = wifi_provisioning_status();
    if (!s_initialized || !wifi_provisioning_should_reconnect(status.mode) ||
        status.active_ssid[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }
    bool blocked;
    taskENTER_CRITICAL(&s_settings_lock);
    const bool busy = s_pending_commit;
    blocked = !busy && wifi_blocked_add(&s_blocked, status.active_ssid);
    taskEXIT_CRITICAL(&s_settings_lock);
    if (busy) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!blocked) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "active network switched off until reboot");
    return wifi_advance_to_next_network();
}

static void scan_set_state(wifi_scan_state_t state)
{
    taskENTER_CRITICAL(&s_scan_lock);
    s_scan_state = state;
    taskEXIT_CRITICAL(&s_scan_lock);
}

esp_err_t wifi_provisioning_scan_start(void)
{
    /* Only while the setup AP is up. Everywhere else there is a stream to
     * interrupt, and the channel hopping a scan does would interrupt it. */
    if (!s_initialized || !s_started ||
        wifi_provisioning_status().mode != WIFI_PROVISIONING_AP_SETUP) {
        return ESP_ERR_INVALID_STATE;
    }
    const TickType_t now = xTaskGetTickCount();
    bool already_running;
    taskENTER_CRITICAL(&s_scan_lock);
    already_running = s_scan_state == WIFI_SCAN_RUNNING;
    if (!already_running) {
        s_scan_state = WIFI_SCAN_RUNNING;
        s_scan_started_tick = now;
    }
    taskEXIT_CRITICAL(&s_scan_lock);
    // A second press while one is in flight joins it rather than starting over.
    if (already_running) {
        return ESP_OK;
    }
    const esp_err_t err = esp_wifi_scan_start(NULL, false);
    if (err != ESP_OK) {
        scan_set_state(WIFI_SCAN_IDLE);
        ESP_LOGE(TAG, "scan failed to start err=%s", esp_err_to_name(err));
    }
    return err;
}

static uint8_t scan_entry_position(const wifi_provisioning_scan_entry_t *entries, uint8_t count,
                                   const char *ssid)
{
    for (uint8_t index = 0; index < count; ++index) {
        if (strcmp(entries[index].ssid, ssid) == 0) {
            return index;
        }
    }
    return count;
}

esp_err_t wifi_provisioning_scan_result(wifi_provisioning_scan_entry_t *entries,
                                        uint8_t capacity, uint8_t *count)
{
    if (entries == NULL || count == NULL || capacity == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    *count = 0U;
    wifi_scan_state_t state;
    TickType_t started;
    taskENTER_CRITICAL(&s_scan_lock);
    state = s_scan_state;
    started = s_scan_started_tick;
    taskEXIT_CRITICAL(&s_scan_lock);
    if (state == WIFI_SCAN_IDLE) {
        return ESP_ERR_INVALID_STATE;
    }
    if (state == WIFI_SCAN_RUNNING) {
        if (xTaskGetTickCount() - started < pdMS_TO_TICKS(WIFI_SCAN_TIMEOUT_MS)) {
            return ESP_ERR_NOT_FINISHED;
        }
        (void)esp_wifi_scan_stop();
        scan_set_state(WIFI_SCAN_IDLE);
        return ESP_ERR_TIMEOUT;
    }

    /* The results are handed over once: esp_wifi_scan_get_ap_records() frees
     * the driver's list as it copies it, so a second read would find nothing.
     * The page keeps what it was given and presses the button again for more. */
    scan_set_state(WIFI_SCAN_IDLE);
    uint16_t found = 0U;
    esp_err_t err = esp_wifi_scan_get_ap_num(&found);
    if (err != ESP_OK) {
        return err;
    }
    if (found == 0U) {
        return ESP_OK;
    }
    if (found > WIFI_PROVISIONING_SCAN_MAX) {
        found = WIFI_PROVISIONING_SCAN_MAX;
    }
    /* Around 80 bytes per record: too much for the stack of the HTTP task that
     * asks for this. */
    wifi_ap_record_t *records = calloc(found, sizeof(*records));
    if (records == NULL) {
        esp_wifi_clear_ap_list();
        return ESP_ERR_NO_MEM;
    }
    err = esp_wifi_scan_get_ap_records(&found, records);
    if (err != ESP_OK) {
        free(records);
        return err;
    }
    for (uint16_t index = 0; index < found && *count < capacity; ++index) {
        char ssid[WIFI_SETTINGS_SSID_MAX_LEN + 1U];
        copy_ssid(ssid, (const char *)records[index].ssid);
        // A hidden network announces an empty name and cannot be joined by
        // picking it off a list; the manual entry is what it is for.
        if (ssid[0] == '\0' || records[index].rssi < WIFI_SCAN_MIN_RSSI) {
            continue;
        }
        // One access point on two bands, or a mesh, appears several times.
        // Keep the strongest sighting of each name.
        const uint8_t existing = scan_entry_position(entries, *count, ssid);
        if (existing < *count) {
            if (records[index].rssi > entries[existing].rssi) {
                entries[existing].rssi = records[index].rssi;
            }
            continue;
        }
        copy_ssid(entries[*count].ssid, ssid);
        entries[*count].rssi = records[index].rssi;
        entries[*count].secure = records[index].authmode != WIFI_AUTH_OPEN;
        ++*count;
    }
    free(records);
    // Strongest first, which is the order the list is worth reading in.
    for (uint8_t index = 1; index < *count; ++index) {
        const wifi_provisioning_scan_entry_t entry = entries[index];
        uint8_t slot = index;
        while (slot > 0U && entries[slot - 1U].rssi < entry.rssi) {
            entries[slot] = entries[slot - 1U];
            --slot;
        }
        entries[slot] = entry;
    }
    return ESP_OK;
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
    s_blocked = (wifi_blocked_ssids_t){0};
    s_current_ssid[0] = '\0';
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

bool wifi_blocked_contains(const wifi_blocked_ssids_t *blocked, const char *ssid)
{
    if (blocked == NULL || ssid == NULL || blocked->count > WIFI_SETTINGS_MAX_NETWORKS) {
        return false;
    }
    for (uint8_t index = 0; index < blocked->count; ++index) {
        if (strcmp(blocked->ssids[index], ssid) == 0) {
            return true;
        }
    }
    return false;
}

bool wifi_blocked_add(wifi_blocked_ssids_t *blocked, const char *ssid)
{
    if (blocked == NULL || ssid == NULL || ssid[0] == '\0' ||
        blocked->count >= WIFI_SETTINGS_MAX_NETWORKS) {
        return false;
    }
    if (wifi_blocked_contains(blocked, ssid)) {
        return true;
    }
    size_t length = 0U;
    while (length < WIFI_SETTINGS_SSID_MAX_LEN && ssid[length] != '\0') {
        blocked->ssids[blocked->count][length] = ssid[length];
        ++length;
    }
    blocked->ssids[blocked->count][length] = '\0';
    ++blocked->count;
    return true;
}

bool wifi_blocked_remove(wifi_blocked_ssids_t *blocked, const char *ssid)
{
    if (!wifi_blocked_contains(blocked, ssid)) {
        return false;
    }
    uint8_t index = 0;
    while (strcmp(blocked->ssids[index], ssid) != 0) {
        ++index;
    }
    for (uint8_t slot = (uint8_t)(index + 1U); slot < blocked->count; ++slot) {
        memcpy(blocked->ssids[slot - 1U], blocked->ssids[slot],
               sizeof(blocked->ssids[0]));
    }
    --blocked->count;
    memset(blocked->ssids[blocked->count], 0, sizeof(blocked->ssids[0]));
    return true;
}

uint8_t wifi_provisioning_next_network(const wifi_settings_t *settings,
                                       const wifi_blocked_ssids_t *blocked,
                                       const char *after_ssid)
{
    if (settings == NULL || settings->count > WIFI_SETTINGS_MAX_NETWORKS) {
        return WIFI_SETTINGS_MAX_NETWORKS;
    }
    uint8_t index = 0;
    if (after_ssid != NULL) {
        const uint8_t previous = wifi_settings_index_of(settings, after_ssid);
        /* A name that is no longer in the list - it was just forgotten, say -
         * starts the walk over rather than ending it: the networks below it
         * have not been tried. */
        if (previous != WIFI_SETTINGS_MAX_NETWORKS) {
            index = (uint8_t)(previous + 1U);
        }
    }
    for (; index < settings->count; ++index) {
        if (!wifi_blocked_contains(blocked, settings->networks[index].ssid)) {
            return index;
        }
    }
    return WIFI_SETTINGS_MAX_NETWORKS;
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
                                     const wifi_blocked_ssids_t *blocked,
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
        output->blocked[index] = wifi_blocked_contains(blocked, output->ssids[index]);
    }
}
