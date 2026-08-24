#include "yandex_api.h"

#include <stdbool.h>
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
/* esp_http_client assembles the request line and headers in this buffer, and
 * defaults it to 512 bytes. A downloadInfoUrl measured 1083 characters, which
 * the client rejects with "Out of buffer" before anything reaches the network. */
#define YANDEX_API_TX_BUFFER_SIZE 2048

static const char *TAG = "yandex_api";

static esp_err_t yandex_api_request(const char *url, bool with_token, bool post,
                                    const char *body, char *response, size_t response_size,
                                    int *status_code, size_t *out_length)
{
    if (url == NULL || response == NULL || response_size == 0U || status_code == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    response[0] = '\0';
    *status_code = 0;
    if (out_length != NULL) *out_length = 0U;

    char token[YANDEX_AUTH_TOKEN_MAX + 1];
    if (with_token && !yandex_auth_copy_token(token, sizeof(token))) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_http_client_config_t config = {
        .url = url,
        .method = post ? HTTP_METHOD_POST : HTTP_METHOD_GET,
        .buffer_size_tx = YANDEX_API_TX_BUFFER_SIZE,
        .timeout_ms = YANDEX_API_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = "jRadio",
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        if (with_token) memset(token, 0, sizeof(token));
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = ESP_OK;
    if (with_token) {
        char authorization[YANDEX_AUTH_TOKEN_MAX + 8];
        snprintf(authorization, sizeof(authorization), "OAuth %s", token);
        memset(token, 0, sizeof(token));
        err = esp_http_client_set_header(client, "Authorization", authorization);
        memset(authorization, 0, sizeof(authorization));
    }
    if (err == ESP_OK) {
        err = esp_http_client_set_header(client, "X-Yandex-Music-Client",
                                         YANDEX_API_CLIENT_HEADER);
    }
    const size_t body_length = body != NULL ? strlen(body) : 0U;
    if (err == ESP_OK && body != NULL) {
        err = esp_http_client_set_header(client, "Content-Type", "application/json");
    }
    if (err == ESP_OK) {
        err = esp_http_client_open(client, (int)body_length);
    }
    if (err == ESP_OK && body_length > 0U &&
        esp_http_client_write(client, body, body_length) != (int)body_length) {
        err = ESP_FAIL;
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
            if (out_length != NULL) *out_length = (size_t)read;
            response[read] = '\0';
            /* A body that exactly fills the buffer is almost certainly cut
             * short, and half a JSON answer parses as nothing useful. Say so
             * here rather than letting the parser report a malformed answer. */
            if ((size_t)read == response_size - 1U) {
                ESP_LOGW(TAG, "%s %.64s filled the whole %u-byte buffer; likely truncated",
                         post ? "POST" : "GET", url, (unsigned int)response_size);
            }
        }
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s %.64s failed: %s", post ? "POST" : "GET", url,
                 esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
    return err;
}

esp_err_t yandex_api_get(const char *path, char *response, size_t response_size,
                         int *status_code)
{
    if (path == NULL) return ESP_ERR_INVALID_ARG;
    char url[YANDEX_API_URL_MAX];
    const int length = snprintf(url, sizeof(url), "https://" YANDEX_API_HOST "%s", path);
    if (length < 0 || (size_t)length >= sizeof(url)) return ESP_ERR_INVALID_ARG;
    return yandex_api_request(url, true, false, NULL, response, response_size, status_code,
                              NULL);
}

esp_err_t yandex_api_post_json(const char *path, const char *body, char *response,
                               size_t response_size, int *status_code)
{
    if (path == NULL || body == NULL) return ESP_ERR_INVALID_ARG;
    char url[YANDEX_API_URL_MAX];
    const int length = snprintf(url, sizeof(url), "https://" YANDEX_API_HOST "%s", path);
    if (length < 0 || (size_t)length >= sizeof(url)) return ESP_ERR_INVALID_ARG;
    return yandex_api_request(url, true, true, body, response, response_size, status_code,
                              NULL);
}

esp_err_t yandex_api_post(const char *path, char *response, size_t response_size,
                          int *status_code)
{
    if (path == NULL) return ESP_ERR_INVALID_ARG;
    char url[YANDEX_API_URL_MAX];
    const int length = snprintf(url, sizeof(url), "https://" YANDEX_API_HOST "%s", path);
    if (length < 0 || (size_t)length >= sizeof(url)) return ESP_ERR_INVALID_ARG;
    return yandex_api_request(url, true, true, NULL, response, response_size, status_code,
                              NULL);
}

esp_err_t yandex_api_get_url(const char *url, char *response, size_t response_size,
                             int *status_code)
{
    return yandex_api_request(url, false, false, NULL, response, response_size, status_code,
                              NULL);
}

esp_err_t yandex_api_get_image(const char *url, uint8_t *buffer, size_t buffer_size,
                               size_t *out_length)
{
    if (out_length == NULL) return ESP_ERR_INVALID_ARG;
    /* One byte is kept back for the terminator the shared helper writes. It is
     * meaningless for a picture, but writing it past the end would not be. */
    if (buffer_size < 2U) return ESP_ERR_INVALID_ARG;
    int status = 0;
    const esp_err_t err = yandex_api_request(url, false, false, NULL, (char *)buffer,
                                            buffer_size, &status, out_length);
    if (err != ESP_OK) return err;
    if (status != 200) {
        *out_length = 0U;
        return ESP_FAIL;
    }
    return ESP_OK;
}
