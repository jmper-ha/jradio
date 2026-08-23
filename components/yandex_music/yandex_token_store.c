#include "yandex_token_store.h"

#include <string.h>

#ifdef ESP_PLATFORM
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cJSON.h"
#include "esp_log.h"
#include "wifi_settings.h"

#define YANDEX_TOKEN_CONFIG_DIR "/littlefs/config"
#define YANDEX_TOKEN_CONFIG_PATH YANDEX_TOKEN_CONFIG_DIR "/yandex.json"
#define YANDEX_TOKEN_TEMP_PATH YANDEX_TOKEN_CONFIG_DIR "/yandex.tmp"

/* Sized from the limits rather than guessed, for the reason wifi.json is:
 * a file that does not fit reads back as damaged, the device then behaves as
 * if it were never authorised, and the next save overwrites the real token
 * with nothing. Six bytes per character is what cJSON costs for a \uXXXX
 * escape, which nothing rejects on the way in. */
#define YANDEX_TOKEN_JSON_ESCAPE_MAX 6U
#define YANDEX_TOKEN_JSON_MAX_LEN                                             \
    (sizeof("{\"version\":1,\"token\":\"\",\"device_id\":\"\"}") +             \
     ((size_t)YANDEX_AUTH_TOKEN_MAX + (size_t)YANDEX_AUTH_DEVICE_ID_MAX) *     \
         YANDEX_TOKEN_JSON_ESCAPE_MAX)

static const char *TAG = "yandex_token";
static bool s_ready;

static void yandex_token_secure_zero(void *memory, size_t size)
{
    volatile unsigned char *bytes = memory;
    while (size-- > 0U) {
        *bytes++ = 0U;
    }
}
#endif

bool yandex_token_is_valid(const char *token)
{
    if (token == NULL) {
        return false;
    }
    size_t length = 0U;
    while (token[length] != '\0') {
        const unsigned char character = (unsigned char)token[length];
        /* Printable ASCII only, and no quote or backslash. Real tokens are
         * alphanumeric; the restriction exists so a malformed answer cannot
         * put a control character or a quote into a JSON file or a log line. */
        if (character < 0x21U || character > 0x7EU || character == '"' ||
            character == '\\') {
            return false;
        }
        if (++length > YANDEX_AUTH_TOKEN_MAX) {
            return false;
        }
    }
    return length > 0U;
}

bool yandex_token_device_id_is_valid(const char *device_id)
{
    if (device_id == NULL) {
        return false;
    }
    size_t length = 0U;
    while (device_id[length] != '\0') {
        const char character = device_id[length];
        const bool alphanumeric = (character >= '0' && character <= '9') ||
                                  (character >= 'a' && character <= 'z') ||
                                  (character >= 'A' && character <= 'Z');
        if (!alphanumeric) {
            return false;
        }
        if (++length > YANDEX_AUTH_DEVICE_ID_MAX) {
            return false;
        }
    }
    return length > 0U;
}

void yandex_token_generate_device_id(char *device_id, size_t capacity,
                                     uint32_t (*next_random)(void))
{
    if (device_id == NULL || capacity == 0U) {
        return;
    }
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    const size_t alphabet_size = sizeof(alphabet) - 1U;
    size_t length = capacity - 1U;
    if (length > YANDEX_AUTH_DEVICE_ID_MAX) {
        length = YANDEX_AUTH_DEVICE_ID_MAX;
    }
    for (size_t index = 0U; index < length; ++index) {
        const uint32_t value = next_random != NULL ? next_random() : 0U;
        device_id[index] = alphabet[value % alphabet_size];
    }
    device_id[length] = '\0';
}

#ifdef ESP_PLATFORM
esp_err_t yandex_token_store_init(void)
{
    /* One mount for the whole partition, owned by wifi_settings; registering it
     * a second time fails. This call is idempotent there. */
    const esp_err_t err = wifi_settings_storage_init();
    s_ready = err == ESP_OK;
    return err;
}

esp_err_t yandex_token_store_load(yandex_token_t *token)
{
    if (token == NULL || !s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    *token = (yandex_token_t){0};

    FILE *file = fopen(YANDEX_TOKEN_CONFIG_PATH, "r");
    if (file == NULL) {
        return ESP_OK;
    }
    char *buffer = malloc(YANDEX_TOKEN_JSON_MAX_LEN);
    if (buffer == NULL) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }
    const size_t bytes_read = fread(buffer, 1, YANDEX_TOKEN_JSON_MAX_LEN - 1U, file);
    const bool truncated = bytes_read == YANDEX_TOKEN_JSON_MAX_LEN - 1U;
    fclose(file);
    buffer[bytes_read] = '\0';

    if (truncated) {
        ESP_LOGE(TAG, "yandex.json is larger than %u bytes; treating as absent",
                 (unsigned int)(YANDEX_TOKEN_JSON_MAX_LEN - 1U));
        yandex_token_secure_zero(buffer, YANDEX_TOKEN_JSON_MAX_LEN);
        free(buffer);
        return ESP_OK;
    }

    cJSON *root = cJSON_Parse(buffer);
    const cJSON *version = root == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(root, "version");
    const cJSON *stored = root == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(root, "token");
    const cJSON *device = root == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(root, "device_id");
    if (cJSON_IsNumber(version) && version->valueint == 1 && cJSON_IsString(stored) &&
        yandex_token_is_valid(stored->valuestring)) {
        strcpy(token->token, stored->valuestring);
        if (cJSON_IsString(device) && yandex_token_device_id_is_valid(device->valuestring)) {
            strcpy(token->device_id, device->valuestring);
        }
    } else {
        ESP_LOGW(TAG, "invalid yandex.json; treating as not authorised");
    }

    if (cJSON_IsString(stored) && stored->valuestring != NULL) {
        yandex_token_secure_zero(stored->valuestring, strlen(stored->valuestring));
    }
    cJSON_Delete(root);
    yandex_token_secure_zero(buffer, YANDEX_TOKEN_JSON_MAX_LEN);
    free(buffer);
    return ESP_OK;
}

esp_err_t yandex_token_store_save(const yandex_token_t *token)
{
    if (token == NULL || !s_ready || !yandex_token_is_valid(token->token)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (mkdir(YANDEX_TOKEN_CONFIG_DIR, 0775) != 0 && errno != EEXIST) {
        return ESP_FAIL;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    const bool built = cJSON_AddNumberToObject(root, "version", 1) != NULL &&
                       cJSON_AddStringToObject(root, "token", token->token) != NULL &&
                       cJSON_AddStringToObject(root, "device_id", token->device_id) != NULL;
    char *json = built ? cJSON_PrintUnformatted(root) : NULL;
    const cJSON *stored = cJSON_GetObjectItemCaseSensitive(root, "token");
    if (cJSON_IsString(stored) && stored->valuestring != NULL) {
        yandex_token_secure_zero(stored->valuestring, strlen(stored->valuestring));
    }
    cJSON_Delete(root);
    if (json == NULL) {
        return ESP_ERR_NO_MEM;
    }

    FILE *file = fopen(YANDEX_TOKEN_TEMP_PATH, "w");
    if (file == NULL) {
        yandex_token_secure_zero(json, strlen(json));
        cJSON_free(json);
        return ESP_FAIL;
    }
    const bool write_ok = fputs(json, file) != EOF;
    const bool close_ok = fclose(file) == 0;
    yandex_token_secure_zero(json, strlen(json));
    cJSON_free(json);
    if (!write_ok || !close_ok) {
        (void)unlink(YANDEX_TOKEN_TEMP_PATH);
        return ESP_FAIL;
    }
    /* Rename over the old file, so a power cut cannot leave a half-written
     * credential behind - the same discipline wifi.json uses. */
    if (rename(YANDEX_TOKEN_TEMP_PATH, YANDEX_TOKEN_CONFIG_PATH) != 0) {
        (void)unlink(YANDEX_TOKEN_TEMP_PATH);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t yandex_token_store_clear(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    return unlink(YANDEX_TOKEN_CONFIG_PATH) == 0 || errno == ENOENT ? ESP_OK : ESP_FAIL;
}
#endif
