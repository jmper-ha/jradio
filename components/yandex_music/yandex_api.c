#include "yandex_api.h"

#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "yandex_auth.h"

/* The header every unofficial client sends; the API answers differently, and
 * sometimes not at all, without it. */
#define YANDEX_API_CLIENT_HEADER "YandexMusicAndroid/24023621"
#define YANDEX_API_TIMEOUT_MS 15000
/* Measured: /rotor/.../tracks is the slowest call at about 530 ms even from a
 * PC on a wired connection, so the timeout is generous on purpose. */
#define YANDEX_API_URL_MAX 256

static const char *TAG = "yandex_api";

esp_err_t yandex_api_get(const char *path, char *response, size_t response_size,
                         int *status_code)
{
    if (path == NULL || response == NULL || response_size == 0U || status_code == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    response[0] = '\0';
    *status_code = 0;

    char token[YANDEX_AUTH_TOKEN_MAX + 1];
    if (!yandex_auth_copy_token(token, sizeof(token))) {
        return ESP_ERR_INVALID_STATE;
    }

    char url[YANDEX_API_URL_MAX];
    const int url_length = snprintf(url, sizeof(url), "https://" YANDEX_API_HOST "%s", path);
    if (url_length < 0 || (size_t)url_length >= sizeof(url)) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = YANDEX_API_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = "jRadio",
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        memset(token, 0, sizeof(token));
        return ESP_ERR_NO_MEM;
    }

    char authorization[YANDEX_AUTH_TOKEN_MAX + 8];
    snprintf(authorization, sizeof(authorization), "OAuth %s", token);
    memset(token, 0, sizeof(token));
    esp_err_t err = esp_http_client_set_header(client, "Authorization", authorization);
    memset(authorization, 0, sizeof(authorization));
    if (err == ESP_OK) {
        err = esp_http_client_set_header(client, "X-Yandex-Music-Client",
                                         YANDEX_API_CLIENT_HEADER);
    }
    if (err == ESP_OK) {
        err = esp_http_client_open(client, 0);
    }
    if (err == ESP_OK && esp_http_client_fetch_headers(client) < 0) {
        err = ESP_FAIL;
    }
    if (err == ESP_OK) {
        *status_code = esp_http_client_get_status_code(client);
        const int read = esp_http_client_read_response(client, response,
                                                       (int)response_size - 1);
        if (read < 0) {
            err = ESP_FAIL;
        } else {
            response[read] = '\0';
            /* A body that exactly fills the buffer is almost certainly cut
             * short, and half a JSON answer parses as nothing useful. Say so
             * here rather than letting the parser report a malformed answer. */
            if ((size_t)read == response_size - 1U) {
                ESP_LOGW(TAG, "GET %s filled the whole %u-byte buffer; likely truncated",
                         path, (unsigned int)response_size);
            }
        }
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "GET %s failed: %s", path, esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
    return err;
}
