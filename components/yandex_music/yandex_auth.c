#include "yandex_auth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "yandex_catalog.h"
#include "yandex_likes.h"
#include "yandex_token_store.h"

/* Public OAuth credentials of the official Yandex Music Android application.
 * They are not secret - every unofficial client uses the same pair - and they
 * are what makes the device-flow endpoint accept us at all. */
#define YANDEX_AUTH_CLIENT_ID "23cabbbdc6cd418abb4b39c32c41195d"
#define YANDEX_AUTH_CLIENT_SECRET "53bc75238f0c4d08a118e51fe9203300"
#define YANDEX_AUTH_DEVICE_CODE_URL "https://oauth.yandex.ru/device/code"
#define YANDEX_AUTH_TOKEN_URL "https://oauth.yandex.ru/token"
/* Shown in the account's device list, so it should read as this box. */
#define YANDEX_AUTH_DEVICE_NAME "jRadio"

/* The answers measured on the real server are 154 and ~250 bytes; this leaves
 * room for a longer error description without putting a kilobyte on the heap
 * for the whole exchange. */
#define YANDEX_AUTH_RESPONSE_MAX 1024U
#define YANDEX_AUTH_BODY_MAX 512U
#define YANDEX_AUTH_HTTP_TIMEOUT_MS 10000
/* The task logs its own high-water mark when it ends, and 6144 measured 1356
 * bytes free on the device through a full TLS handshake and a cancel. That is
 * the same margin that had `ui` raised after it nearly overflowed, and the
 * deepest path here - parsing the token, printing JSON and writing it to
 * LittleFS - only runs once the user confirms, so it was not in that figure.
 * Transient anyway: the task exists only while linking an account. */
#define YANDEX_AUTH_TASK_STACK 8192
#define YANDEX_AUTH_TASK_PRIORITY 4
/* The cancel flag is checked between polls, so the wait must stay short enough
 * that a cancelled screen does not sit there for the whole poll interval. */
#define YANDEX_AUTH_TICK_MS 200

static const char *TAG = "yandex_auth";

static SemaphoreHandle_t s_lock;
static yandex_auth_status_t s_status;
static yandex_auth_flow_t s_flow;
static yandex_token_t s_token;
static char s_device_code[YANDEX_AUTH_DEVICE_CODE_MAX + 1];
static TaskHandle_t s_task;
static volatile bool s_cancel;

static void yandex_auth_secure_zero(void *memory, size_t size)
{
    volatile unsigned char *bytes = memory;
    while (size-- > 0U) {
        *bytes++ = 0U;
    }
}

static void yandex_auth_lock(void)
{
    if (s_lock != NULL) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

static void yandex_auth_unlock(void)
{
    if (s_lock != NULL) {
        xSemaphoreGive(s_lock);
    }
}

static uint32_t yandex_auth_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void yandex_auth_set_failure(yandex_auth_error_t error)
{
    yandex_auth_lock();
    s_status.state = YANDEX_AUTH_FAILED;
    s_status.error = error;
    s_status.user_code[0] = '\0';
    s_status.verification_url[0] = '\0';
    s_status.seconds_left = 0U;
    s_flow.state = YANDEX_AUTH_FAILED;
    yandex_auth_unlock();
}

/* Percent-encodes into an x-www-form-urlencoded value. The device code is an
 * opaque string from the server, so it cannot be pasted into a body raw
 * without risking a stray '&' or '+' changing which field it lands in. */
static bool yandex_auth_encode(char *destination, size_t capacity, const char *value)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t written = 0U;
    for (const char *cursor = value; *cursor != '\0'; ++cursor) {
        const unsigned char character = (unsigned char)*cursor;
        const bool unreserved = (character >= '0' && character <= '9') ||
                                (character >= 'a' && character <= 'z') ||
                                (character >= 'A' && character <= 'Z') ||
                                character == '-' || character == '_' || character == '.' ||
                                character == '~';
        if (unreserved) {
            if (written + 2U > capacity) return false;
            destination[written++] = (char)character;
        } else {
            if (written + 4U > capacity) return false;
            destination[written++] = '%';
            destination[written++] = hex[character >> 4U];
            destination[written++] = hex[character & 0x0FU];
        }
    }
    if (written + 1U > capacity) return false;
    destination[written] = '\0';
    return true;
}

/* Returns the body for any status code, not only 2xx: the device flow answers
 * "not confirmed yet" as HTTP 400 with a JSON body, and that is the message we
 * spend most of the exchange reading. */
static esp_err_t yandex_auth_post(const char *url, const char *body, char *response,
                                  size_t response_size, int *status_code)
{
    const esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = YANDEX_AUTH_HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = YANDEX_AUTH_DEVICE_NAME,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = esp_http_client_set_header(client, "Content-Type",
                                               "application/x-www-form-urlencoded");
    const int body_length = (int)strlen(body);
    if (err == ESP_OK) {
        err = esp_http_client_open(client, body_length);
    }
    if (err == ESP_OK && esp_http_client_write(client, body, body_length) != body_length) {
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
            response[read] = '\0';
        }
    }
    if (err != ESP_OK) {
        /* No response body is logged anywhere in this file: on the success path
         * it contains the token. */
        ESP_LOGW(TAG, "POST %s failed: %s", url, esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
    return err;
}

static bool yandex_auth_request_code(char *response)
{
    char device_id[YANDEX_AUTH_DEVICE_ID_MAX + 1];
    yandex_auth_lock();
    strcpy(device_id, s_token.device_id);
    yandex_auth_unlock();
    if (!yandex_token_device_id_is_valid(device_id)) {
        yandex_token_generate_device_id(device_id, sizeof(device_id), esp_random);
        yandex_auth_lock();
        strcpy(s_token.device_id, device_id);
        yandex_auth_unlock();
    }

    char body[YANDEX_AUTH_BODY_MAX];
    const int length = snprintf(body, sizeof(body),
                                "client_id=" YANDEX_AUTH_CLIENT_ID
                                "&device_id=%s&device_name=" YANDEX_AUTH_DEVICE_NAME,
                                device_id);
    if (length < 0 || (size_t)length >= sizeof(body)) {
        yandex_auth_set_failure(YANDEX_AUTH_ERROR_SERVER);
        return false;
    }

    int status_code = 0;
    if (yandex_auth_post(YANDEX_AUTH_DEVICE_CODE_URL, body, response,
                         YANDEX_AUTH_RESPONSE_MAX, &status_code) != ESP_OK) {
        yandex_auth_set_failure(YANDEX_AUTH_ERROR_NETWORK);
        return false;
    }

    yandex_auth_device_code_t code;
    if (!yandex_auth_parse_device_code(response, &code)) {
        ESP_LOGE(TAG, "device code request rejected, HTTP %d", status_code);
        yandex_auth_set_failure(YANDEX_AUTH_ERROR_SERVER);
        return false;
    }

    const uint32_t now_ms = yandex_auth_now_ms();
    yandex_auth_lock();
    strcpy(s_device_code, code.device_code);
    snprintf(s_status.user_code, sizeof(s_status.user_code), "%s", code.user_code);
    snprintf(s_status.verification_url, sizeof(s_status.verification_url), "%s",
             code.verification_url);
    s_status.state = YANDEX_AUTH_WAITING;
    s_status.error = YANDEX_AUTH_ERROR_NONE;
    s_flow = (yandex_auth_flow_t){
        .state = YANDEX_AUTH_WAITING,
        .issued_ms = now_ms,
        .last_poll_ms = now_ms,
        .interval_ms = yandex_auth_flow_clamp_interval(code.interval_s),
        .lifetime_ms = yandex_auth_flow_clamp_lifetime(code.expires_in_s),
        .poll_in_flight = false,
    };
    s_status.seconds_left = yandex_auth_flow_seconds_left(&s_flow, now_ms);
    yandex_auth_unlock();

    /* The code itself is safe to log - it is useless without the user typing
     * it into their own account within five minutes, and having it in the
     * serial log makes a failed screen diagnosable. */
    ESP_LOGI(TAG, "device code issued: %s at %s, valid %us", code.user_code,
             code.verification_url, (unsigned int)(s_flow.lifetime_ms / 1000U));
    return true;
}

/* Returns false when the exchange is over, one way or the other. */
static bool yandex_auth_poll_once(char *response)
{
    char device_code[YANDEX_AUTH_DEVICE_CODE_MAX + 1];
    yandex_auth_lock();
    strcpy(device_code, s_device_code);
    s_flow.poll_in_flight = true;
    s_flow.last_poll_ms = yandex_auth_now_ms();
    yandex_auth_unlock();

    char encoded[YANDEX_AUTH_DEVICE_CODE_MAX * 3U + 1U];
    char body[YANDEX_AUTH_BODY_MAX];
    bool built = yandex_auth_encode(encoded, sizeof(encoded), device_code);
    if (built) {
        const int length = snprintf(body, sizeof(body),
                                    "grant_type=device_code&code=%s"
                                    "&client_id=" YANDEX_AUTH_CLIENT_ID
                                    "&client_secret=" YANDEX_AUTH_CLIENT_SECRET,
                                    encoded);
        built = length > 0 && (size_t)length < sizeof(body);
    }
    yandex_auth_secure_zero(encoded, sizeof(encoded));
    if (!built) {
        yandex_auth_set_failure(YANDEX_AUTH_ERROR_SERVER);
        return false;
    }

    int status_code = 0;
    const esp_err_t err = yandex_auth_post(YANDEX_AUTH_TOKEN_URL, body, response,
                                           YANDEX_AUTH_RESPONSE_MAX, &status_code);
    yandex_auth_secure_zero(body, sizeof(body));
    if (err != ESP_OK) {
        /* A single failed request is not the end: Wi-Fi drops out and comes
         * back, and the code is valid for minutes. Keep polling until it
         * expires; only then report a failure. */
        yandex_auth_lock();
        s_flow.poll_in_flight = false;
        yandex_auth_unlock();
        return true;
    }

    char token[YANDEX_AUTH_TOKEN_MAX + 1];
    const yandex_auth_token_result_t result =
        yandex_auth_parse_token(response, token, sizeof(token));
    yandex_auth_secure_zero(response, YANDEX_AUTH_RESPONSE_MAX);

    bool keep_polling = true;
    switch (result) {
    case YANDEX_AUTH_TOKEN_OK: {
        yandex_auth_lock();
        strcpy(s_token.token, token);
        const yandex_token_t to_save = s_token;
        yandex_auth_unlock();
        const esp_err_t saved = yandex_token_store_save(&to_save);
        if (saved == ESP_OK) {
            yandex_auth_lock();
            s_status.state = YANDEX_AUTH_AUTHORIZED;
            s_status.error = YANDEX_AUTH_ERROR_NONE;
            s_status.user_code[0] = '\0';
            s_status.verification_url[0] = '\0';
            s_status.seconds_left = 0U;
            s_flow.state = YANDEX_AUTH_AUTHORIZED;
            yandex_auth_unlock();
            ESP_LOGI(TAG, "authorised");
        } else {
            /* The token is good but unwritable, so it would be lost on the next
             * boot. Saying "authorised" here would be a lie that only shows up
             * after a power cycle. */
            ESP_LOGE(TAG, "token could not be stored: %s", esp_err_to_name(saved));
            yandex_auth_lock();
            yandex_auth_secure_zero(s_token.token, sizeof(s_token.token));
            yandex_auth_unlock();
            yandex_auth_set_failure(YANDEX_AUTH_ERROR_STORAGE);
        }
        keep_polling = false;
        break;
    }
    case YANDEX_AUTH_TOKEN_PENDING:
        break;
    case YANDEX_AUTH_TOKEN_SLOW_DOWN:
        yandex_auth_lock();
        s_flow.interval_ms = yandex_auth_flow_backoff(s_flow.interval_ms);
        yandex_auth_unlock();
        ESP_LOGW(TAG, "server asked to slow down");
        break;
    case YANDEX_AUTH_TOKEN_DENIED:
        ESP_LOGW(TAG, "authorisation refused");
        yandex_auth_set_failure(YANDEX_AUTH_ERROR_DENIED);
        keep_polling = false;
        break;
    case YANDEX_AUTH_TOKEN_EXPIRED:
        yandex_auth_set_failure(YANDEX_AUTH_ERROR_TIMEOUT);
        keep_polling = false;
        break;
    case YANDEX_AUTH_TOKEN_INVALID:
    default:
        ESP_LOGE(TAG, "unexpected token answer, HTTP %d", status_code);
        yandex_auth_set_failure(YANDEX_AUTH_ERROR_SERVER);
        keep_polling = false;
        break;
    }

    yandex_auth_secure_zero(token, sizeof(token));
    yandex_auth_lock();
    s_flow.poll_in_flight = false;
    yandex_auth_unlock();
    return keep_polling;
}

static void yandex_auth_task(void *context)
{
    (void)context;
    char *response = malloc(YANDEX_AUTH_RESPONSE_MAX);
    if (response == NULL) {
        yandex_auth_set_failure(YANDEX_AUTH_ERROR_SERVER);
    } else if (yandex_auth_request_code(response)) {
        while (!s_cancel) {
            const uint32_t now_ms = yandex_auth_now_ms();
            yandex_auth_lock();
            s_status.seconds_left = yandex_auth_flow_seconds_left(&s_flow, now_ms);
            const yandex_auth_flow_t flow = s_flow;
            yandex_auth_unlock();

            const yandex_auth_action_t action = yandex_auth_flow_next(&flow, now_ms);
            if (action == YANDEX_AUTH_ACTION_EXPIRE) {
                ESP_LOGW(TAG, "device code expired before it was confirmed");
                yandex_auth_set_failure(YANDEX_AUTH_ERROR_TIMEOUT);
                break;
            }
            if (action == YANDEX_AUTH_ACTION_POLL_TOKEN && !yandex_auth_poll_once(response)) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(YANDEX_AUTH_TICK_MS));
        }
    }

    if (response != NULL) {
        yandex_auth_secure_zero(response, YANDEX_AUTH_RESPONSE_MAX);
        free(response);
    }
    yandex_auth_lock();
    yandex_auth_secure_zero(s_device_code, sizeof(s_device_code));
    if (s_cancel && s_status.state == YANDEX_AUTH_WAITING) {
        s_status.state = yandex_token_is_valid(s_token.token) ? YANDEX_AUTH_AUTHORIZED
                                                              : YANDEX_AUTH_IDLE;
        s_status.user_code[0] = '\0';
        s_status.verification_url[0] = '\0';
        s_status.seconds_left = 0U;
        s_flow.state = s_status.state;
    }
    s_task = NULL;
    yandex_auth_unlock();

    ESP_LOGI(TAG, "auth task done, stack headroom %u",
             (unsigned int)uxTaskGetStackHighWaterMark(NULL));
    vTaskDelete(NULL);
}

esp_err_t yandex_auth_init(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    const esp_err_t err = yandex_token_store_init();
    if (err != ESP_OK) {
        return err;
    }
    yandex_token_t stored = {0};
    (void)yandex_token_store_load(&stored);

    yandex_auth_lock();
    s_token = stored;
    s_status = (yandex_auth_status_t){0};
    s_status.state = yandex_token_is_valid(s_token.token) ? YANDEX_AUTH_AUTHORIZED
                                                          : YANDEX_AUTH_IDLE;
    s_flow.state = s_status.state;
    yandex_auth_unlock();
    yandex_auth_secure_zero(&stored, sizeof(stored));

    ESP_LOGI(TAG, "%s", s_status.state == YANDEX_AUTH_AUTHORIZED ? "token found"
                                                                 : "no token stored");
    return ESP_OK;
}

esp_err_t yandex_auth_begin(void)
{
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    yandex_auth_lock();
    const bool busy = s_task != NULL;
    if (!busy) {
        /* Both the screen and the web UI can ask; the second asker should see
         * the attempt that is already running, not start a competing one. */
        s_status.state = YANDEX_AUTH_REQUESTING;
        s_status.error = YANDEX_AUTH_ERROR_NONE;
        s_status.user_code[0] = '\0';
        s_status.verification_url[0] = '\0';
        s_status.seconds_left = 0U;
        s_flow.state = YANDEX_AUTH_REQUESTING;
    }
    yandex_auth_unlock();
    if (busy) {
        return ESP_OK;
    }

    s_cancel = false;
    TaskHandle_t task = NULL;
    /* Core 0: core 1 is reserved for the decoder, and a TLS handshake there
     * would compete with it for cycles. */
    if (xTaskCreatePinnedToCore(yandex_auth_task, "yandex_auth", YANDEX_AUTH_TASK_STACK, NULL,
                                YANDEX_AUTH_TASK_PRIORITY, &task, 0) != pdPASS) {
        yandex_auth_set_failure(YANDEX_AUTH_ERROR_SERVER);
        return ESP_ERR_NO_MEM;
    }
    yandex_auth_lock();
    s_task = task;
    yandex_auth_unlock();
    return ESP_OK;
}

esp_err_t yandex_auth_cancel(void)
{
    s_cancel = true;
    return ESP_OK;
}

esp_err_t yandex_auth_forget(void)
{
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_cancel = true;
    const esp_err_t err = yandex_token_store_clear();
    yandex_auth_lock();
    yandex_auth_secure_zero(s_token.token, sizeof(s_token.token));
    s_status = (yandex_auth_status_t){0};
    s_status.state = YANDEX_AUTH_IDLE;
    s_flow.state = YANDEX_AUTH_IDLE;
    yandex_auth_unlock();
    /* Here rather than in the screen that pressed the button: the web UI can
     * unlink too, and leaving one account's stations listed after the other
     * surface logged out would be a small betrayal. */
    yandex_catalog_clear();
    /* And the account id the likes path is built from, for the same reason:
     * the next account to be linked must not be handed the previous one's. */
    yandex_likes_forget();
    ESP_LOGI(TAG, "token forgotten");
    return err;
}

yandex_auth_status_t yandex_auth_get_status(void)
{
    yandex_auth_status_t status = {0};
    yandex_auth_lock();
    status = s_status;
    if (status.state == YANDEX_AUTH_WAITING) {
        status.seconds_left = yandex_auth_flow_seconds_left(&s_flow, yandex_auth_now_ms());
    }
    yandex_auth_unlock();
    return status;
}

bool yandex_auth_is_authorized(void)
{
    yandex_auth_lock();
    const bool authorized = yandex_token_is_valid(s_token.token);
    yandex_auth_unlock();
    return authorized;
}

bool yandex_auth_copy_token(char *token, size_t token_size)
{
    if (token == NULL || token_size == 0U) {
        return false;
    }
    token[0] = '\0';
    yandex_auth_lock();
    const bool ok = yandex_token_is_valid(s_token.token) &&
                    strlen(s_token.token) < token_size;
    if (ok) {
        strcpy(token, s_token.token);
    }
    yandex_auth_unlock();
    return ok;
}
