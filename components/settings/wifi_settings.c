#include "wifi_settings.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cJSON.h"
#include "esp_littlefs.h"
#include "esp_log.h"

#define WIFI_SETTINGS_MOUNT_PATH "/littlefs"
#define WIFI_SETTINGS_CONFIG_DIR WIFI_SETTINGS_MOUNT_PATH "/config"
#define WIFI_SETTINGS_CONFIG_PATH WIFI_SETTINGS_CONFIG_DIR "/wifi.json"
#define WIFI_SETTINGS_TEMP_PATH WIFI_SETTINGS_CONFIG_DIR "/wifi.tmp"

static const char *TAG = "wifi_settings";
static bool s_storage_mounted;
#endif

static bool wifi_settings_is_valid_string(const char *value, size_t max_length, bool allow_empty)
{
    if (value == NULL) {
        return false;
    }
    size_t length = 0;
    while (length <= max_length && value[length] != '\0') {
        ++length;
    }
    return length <= max_length && (allow_empty || length > 0);
}

wifi_settings_result_t wifi_settings_upsert(wifi_settings_t *settings, const char *ssid,
                                            const char *password)
{
    if (settings == NULL || settings->count > WIFI_SETTINGS_MAX_NETWORKS ||
        !wifi_settings_is_valid_string(ssid, WIFI_SETTINGS_SSID_MAX_LEN, false) ||
        !wifi_settings_is_valid_string(password, WIFI_SETTINGS_PASSWORD_MAX_LEN, true)) {
        return WIFI_SETTINGS_INVALID_ARG;
    }

    for (uint8_t index = 0; index < settings->count; ++index) {
        if (strcmp(settings->networks[index].ssid, ssid) == 0) {
            strcpy(settings->networks[index].password, password);
            return WIFI_SETTINGS_OK;
        }
    }
    if (settings->count == WIFI_SETTINGS_MAX_NETWORKS) {
        return WIFI_SETTINGS_FULL;
    }

    wifi_network_t *network = &settings->networks[settings->count++];
    strcpy(network->ssid, ssid);
    strcpy(network->password, password);
    return WIFI_SETTINGS_OK;
}

#ifdef ESP_PLATFORM
esp_err_t wifi_settings_storage_init(void)
{
    if (s_storage_mounted) {
        return ESP_OK;
    }
    const esp_vfs_littlefs_conf_t config = {
        .base_path = WIFI_SETTINGS_MOUNT_PATH,
        .partition_label = "littlefs",
        /* Configuration and web assets share this partition. Never erase it
         * automatically when a mount fails; surface the error instead. */
        .format_if_mount_failed = false,
    };
    const esp_err_t err = esp_vfs_littlefs_register(&config);
    if (err == ESP_OK) {
        s_storage_mounted = true;
    }
    return err;
}

static bool wifi_settings_json_read_string(const cJSON *item, const char *name, char *value,
                                           size_t max_length, bool allow_empty)
{
    const cJSON *json_string = cJSON_GetObjectItemCaseSensitive(item, name);
    if (!cJSON_IsString(json_string) || json_string->valuestring == NULL ||
        !wifi_settings_is_valid_string(json_string->valuestring, max_length, allow_empty)) {
        return false;
    }
    strcpy(value, json_string->valuestring);
    return true;
}

esp_err_t wifi_settings_load(wifi_settings_t *settings)
{
    if (settings == NULL || !s_storage_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    *settings = (wifi_settings_t){0};
    FILE *file = fopen(WIFI_SETTINGS_CONFIG_PATH, "r");
    if (file == NULL) {
        return ESP_OK;
    }
    char buffer[1024];
    const size_t bytes_read = fread(buffer, 1, sizeof(buffer) - 1, file);
    fclose(file);
    buffer[bytes_read] = '\0';

    cJSON *root = cJSON_Parse(buffer);
    const cJSON *version = root == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(root, "version");
    const cJSON *networks = root == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(root, "networks");
    if (!cJSON_IsNumber(version) || version->valueint != 1 || !cJSON_IsArray(networks)) {
        ESP_LOGW(TAG, "invalid wifi.json; using empty settings");
        cJSON_Delete(root);
        return ESP_OK;
    }
    const cJSON *network = NULL;
    cJSON_ArrayForEach(network, networks) {
        wifi_network_t parsed = {0};
        if (!wifi_settings_json_read_string(network, "ssid", parsed.ssid, WIFI_SETTINGS_SSID_MAX_LEN,
                                            false) ||
            !wifi_settings_json_read_string(network, "password", parsed.password,
                                            WIFI_SETTINGS_PASSWORD_MAX_LEN, true) ||
            wifi_settings_upsert(settings, parsed.ssid, parsed.password) != WIFI_SETTINGS_OK) {
            *settings = (wifi_settings_t){0};
            ESP_LOGW(TAG, "invalid wifi.json; using empty settings");
            break;
        }
    }
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t wifi_settings_save_atomic(const wifi_settings_t *settings)
{
    if (settings == NULL || !s_storage_mounted || settings->count > WIFI_SETTINGS_MAX_NETWORKS) {
        return ESP_ERR_INVALID_ARG;
    }
    for (uint8_t index = 0; index < settings->count; ++index) {
        if (!wifi_settings_is_valid_string(settings->networks[index].ssid,
                                           WIFI_SETTINGS_SSID_MAX_LEN, false) ||
            !wifi_settings_is_valid_string(settings->networks[index].password,
                                           WIFI_SETTINGS_PASSWORD_MAX_LEN, true)) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    if (mkdir(WIFI_SETTINGS_CONFIG_DIR, 0775) != 0 && errno != EEXIST) {
        return ESP_FAIL;
    }
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON *networks = cJSON_AddArrayToObject(root, "networks");
    if (networks == NULL || cJSON_AddNumberToObject(root, "version", 1) == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    for (uint8_t index = 0; index < settings->count; ++index) {
        cJSON *network = cJSON_CreateObject();
        if (network == NULL) {
            cJSON_Delete(root);
            return ESP_ERR_NO_MEM;
        }
        if (cJSON_AddStringToObject(network, "ssid", settings->networks[index].ssid) == NULL ||
            cJSON_AddStringToObject(network, "password",
                                    settings->networks[index].password) == NULL ||
            !cJSON_AddItemToArray(networks, network)) {
            cJSON_Delete(network);
            cJSON_Delete(root);
            return ESP_ERR_NO_MEM;
        }
    }
    char *json = root == NULL ? NULL : cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) {
        return ESP_ERR_NO_MEM;
    }
    FILE *file = fopen(WIFI_SETTINGS_TEMP_PATH, "w");
    if (file == NULL) {
        cJSON_free(json);
        return ESP_FAIL;
    }
    const bool write_ok = fputs(json, file) != EOF;
    const bool close_ok = fclose(file) == 0;
    cJSON_free(json);
    if (!write_ok || !close_ok) {
        (void)unlink(WIFI_SETTINGS_TEMP_PATH);
        return ESP_FAIL;
    }
    if (rename(WIFI_SETTINGS_TEMP_PATH, WIFI_SETTINGS_CONFIG_PATH) != 0) {
        (void)unlink(WIFI_SETTINGS_TEMP_PATH);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t wifi_settings_clear(void)
{
    if (!s_storage_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    return unlink(WIFI_SETTINGS_CONFIG_PATH) == 0 || errno == ENOENT ? ESP_OK : ESP_FAIL;
}
#endif
