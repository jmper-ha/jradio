#include "wifi_settings.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static void wifi_settings_secure_zero(void *memory, size_t size)
{
    volatile unsigned char *bytes = memory;
    while (size-- > 0U) {
        *bytes++ = 0U;
    }
}

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

/* Enough for the largest file the writer below can produce: the wrapper, plus
 * every network at its maximum length with every single character escaped as
 * \uXXXX - six bytes each, which is what cJSON does for a control character
 * and nothing rejects one on the way in.
 *
 * Derived from the limits rather than picked, because a file that does not fit
 * is worse than unreadable. A truncated read fails to parse, the device then
 * reports that no networks are saved, and the first save afterwards overwrites
 * the file with that - the credentials are gone without anything having said
 * so. The previous fixed 1024 bytes was under the worst case for five
 * networks. */
#define WIFI_SETTINGS_JSON_ESCAPE_MAX 6U
#define WIFI_SETTINGS_JSON_MAX_LEN                                                 \
    (sizeof("{\"version\":1,\"networks\":[]}") +                                  \
     (size_t)WIFI_SETTINGS_MAX_NETWORKS *                                          \
         (sizeof("{\"ssid\":\"\",\"password\":\"\"},") +                           \
          ((size_t)WIFI_SETTINGS_SSID_MAX_LEN + (size_t)WIFI_SETTINGS_PASSWORD_MAX_LEN) * \
              WIFI_SETTINGS_JSON_ESCAPE_MAX))

static const char *TAG = "wifi_settings";
static bool s_storage_mounted;

static void wifi_settings_wipe_json(cJSON *item)
{
    for (cJSON *current = item; current != NULL; current = current->next) {
        if (current->string != NULL && strcmp(current->string, "password") == 0 &&
            current->valuestring != NULL) {
            wifi_settings_secure_zero(current->valuestring,
                                      strlen(current->valuestring));
        }
        if (current->child != NULL) {
            wifi_settings_wipe_json(current->child);
        }
    }
}

static void wifi_settings_delete_json(cJSON *root)
{
    wifi_settings_wipe_json(root);
    cJSON_Delete(root);
}

static void wifi_settings_free_text(char *text)
{
    if (text != NULL) {
        wifi_settings_secure_zero(text, strlen(text));
        cJSON_free(text);
    }
}
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

uint8_t wifi_settings_index_of(const wifi_settings_t *settings, const char *ssid)
{
    if (settings == NULL || ssid == NULL || settings->count > WIFI_SETTINGS_MAX_NETWORKS) {
        return WIFI_SETTINGS_MAX_NETWORKS;
    }
    for (uint8_t index = 0; index < settings->count; ++index) {
        if (strcmp(settings->networks[index].ssid, ssid) == 0) {
            return index;
        }
    }
    return WIFI_SETTINGS_MAX_NETWORKS;
}

bool wifi_settings_remove(wifi_settings_t *settings, const char *ssid)
{
    const uint8_t index = wifi_settings_index_of(settings, ssid);
    if (index == WIFI_SETTINGS_MAX_NETWORKS) {
        return false;
    }
    for (uint8_t slot = (uint8_t)(index + 1U); slot < settings->count; ++slot) {
        settings->networks[slot - 1U] = settings->networks[slot];
    }
    --settings->count;
    /* The slot the list no longer counts still holds a copy of a password, and
     * the whole structure gets copied around by value. */
    wifi_settings_secure_zero(&settings->networks[settings->count],
                              sizeof(settings->networks[0]));
    return true;
}

bool wifi_settings_promote(wifi_settings_t *settings, const char *ssid)
{
    const uint8_t index = wifi_settings_index_of(settings, ssid);
    if (index == WIFI_SETTINGS_MAX_NETWORKS) {
        return false;
    }
    wifi_network_t promoted = settings->networks[index];
    for (uint8_t slot = index; slot > 0U; --slot) {
        settings->networks[slot] = settings->networks[slot - 1U];
    }
    settings->networks[0] = promoted;
    wifi_settings_secure_zero(&promoted, sizeof(promoted));
    return true;
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
    /* On the heap, not the stack: this runs from app_main before any task has
     * spare depth, and the buffer is now several kilobytes. */
    char *buffer = malloc(WIFI_SETTINGS_JSON_MAX_LEN);
    if (buffer == NULL) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }
    const size_t bytes_read = fread(buffer, 1, WIFI_SETTINGS_JSON_MAX_LEN - 1U, file);
    const bool truncated = bytes_read == WIFI_SETTINGS_JSON_MAX_LEN - 1U;
    fclose(file);
    buffer[bytes_read] = '\0';

    if (truncated) {
        /* Larger than anything this component can write, so the file is damaged
         * or foreign. Say so loudly and distinctly from "no networks yet": the
         * two look identical from the outside and lead to very different
         * conclusions about where the credentials went. */
        ESP_LOGE(TAG, "wifi.json is larger than %u bytes; it will be treated as empty",
                 (unsigned int)(WIFI_SETTINGS_JSON_MAX_LEN - 1U));
        wifi_settings_secure_zero(buffer, WIFI_SETTINGS_JSON_MAX_LEN);
        free(buffer);
        return ESP_OK;
    }

    cJSON *root = cJSON_Parse(buffer);
    const cJSON *version = root == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(root, "version");
    const cJSON *networks = root == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(root, "networks");
    if (!cJSON_IsNumber(version) || version->valueint != 1 || !cJSON_IsArray(networks)) {
        ESP_LOGW(TAG, "invalid wifi.json; using empty settings");
        wifi_settings_delete_json(root);
        wifi_settings_secure_zero(buffer, WIFI_SETTINGS_JSON_MAX_LEN);
        free(buffer);
        return ESP_OK;
    }
    const cJSON *network = NULL;
    cJSON_ArrayForEach(network, networks) {
        wifi_network_t parsed = {0};
        const bool valid =
            wifi_settings_json_read_string(network, "ssid", parsed.ssid,
                                           WIFI_SETTINGS_SSID_MAX_LEN, false) &&
            wifi_settings_json_read_string(network, "password", parsed.password,
                                           WIFI_SETTINGS_PASSWORD_MAX_LEN, true) &&
            wifi_settings_upsert(settings, parsed.ssid, parsed.password) ==
                WIFI_SETTINGS_OK;
        wifi_settings_secure_zero(&parsed, sizeof(parsed));
        if (!valid) {
            *settings = (wifi_settings_t){0};
            ESP_LOGW(TAG, "invalid wifi.json; using empty settings");
            break;
        }
    }
    wifi_settings_delete_json(root);
    wifi_settings_secure_zero(buffer, WIFI_SETTINGS_JSON_MAX_LEN);
    free(buffer);
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
        wifi_settings_delete_json(root);
        return ESP_ERR_NO_MEM;
    }
    for (uint8_t index = 0; index < settings->count; ++index) {
        cJSON *network = cJSON_CreateObject();
        if (network == NULL) {
            wifi_settings_delete_json(root);
            return ESP_ERR_NO_MEM;
        }
        if (cJSON_AddStringToObject(network, "ssid", settings->networks[index].ssid) == NULL ||
            cJSON_AddStringToObject(network, "password",
                                    settings->networks[index].password) == NULL ||
            !cJSON_AddItemToArray(networks, network)) {
            wifi_settings_delete_json(network);
            wifi_settings_delete_json(root);
            return ESP_ERR_NO_MEM;
        }
    }
    char *json = root == NULL ? NULL : cJSON_PrintUnformatted(root);
    wifi_settings_delete_json(root);
    if (json == NULL) {
        return ESP_ERR_NO_MEM;
    }
    FILE *file = fopen(WIFI_SETTINGS_TEMP_PATH, "w");
    if (file == NULL) {
        wifi_settings_free_text(json);
        return ESP_FAIL;
    }
    const bool write_ok = fputs(json, file) != EOF;
    const bool close_ok = fclose(file) == 0;
    wifi_settings_free_text(json);
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
