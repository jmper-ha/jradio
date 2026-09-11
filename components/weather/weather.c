#include "weather.h"

#ifdef ESP_PLATFORM

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "wifi_provisioning.h"
#include "wifi_settings.h"

static const char *TAG = "weather";

#define WEATHER_CONFIG_DIR "/littlefs/config"
#define WEATHER_KEY_PATH WEATHER_CONFIG_DIR "/weather.json"
#define WEATHER_KEY_TEMP_PATH WEATHER_CONFIG_DIR "/weather.tmp"
/* The file is one string, and this is that string escaped six bytes a
 * character, which is the most cJSON can make of it. */
#define WEATHER_KEY_JSON_MAX (64U + (size_t)WEATHER_KEY_MAX * 6U)

/* Once a quarter of an hour. Open-Meteo allows ten thousand a day and updates
 * every fifteen minutes itself; wttr.in caches for ten; the weather does not
 * change faster than that on a status strip. */
#define WEATHER_POLL_MS (15U * 60U * 1000U)
/* After a failure: a minute the first time, four minutes from then on, so a
 * service that hiccuped is back within a minute and one that is down is not
 * hammered. */
#define WEATHER_RETRY_MIN_MS (60U * 1000U)
/* Before the network is up there is nothing to ask; this is how often to
 * look. */
#define WEATHER_NO_NETWORK_MS (10U * 1000U)
/* How long a report stays on the strip after the service stops answering. Two
 * hours of yesterday's weather is a guess the reader would make anyway; past
 * that the strip is better empty. */
#define WEATHER_STALE_MS (2U * 60U * 60U * 1000U)

/* The largest answer is OpenWeatherMap's at about 500 bytes. */
#define WEATHER_RESPONSE_MAX 2048U
#define WEATHER_HTTP_TIMEOUT_MS 10000
/* Plain HTTP and a kilobyte of JSON: half of what the Yandex catalog's TLS
 * fetch needs. Measured at the first fetch and logged, see below. */
#define WEATHER_TASK_STACK 5120
#define WEATHER_TASK_PRIORITY 3

typedef struct {
    device_weather_provider_t provider;
    char latitude[DEVICE_COORDINATE_MAX];
    char longitude[DEVICE_COORDINATE_MAX];
    char key[WEATHER_KEY_MAX + 1];
} weather_config_t;

static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;
static weather_config_t s_config;
static weather_status_t s_status;
static int64_t s_report_at_ms;
static bool s_stack_reported;

static void weather_secure_zero(void *memory, size_t size)
{
    volatile unsigned char *bytes = memory;
    while (size-- > 0U) *bytes++ = 0U;
}

static void lock(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s_lock);
}

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

/* --- the key file ------------------------------------------------------ */

static void weather_key_load(char *key, size_t size)
{
    key[0] = '\0';
    FILE *file = fopen(WEATHER_KEY_PATH, "r");
    if (file == NULL) return;
    char *buffer = malloc(WEATHER_KEY_JSON_MAX);
    if (buffer == NULL) {
        fclose(file);
        return;
    }
    const size_t read = fread(buffer, 1U, WEATHER_KEY_JSON_MAX - 1U, file);
    fclose(file);
    buffer[read] = '\0';
    cJSON *root = cJSON_Parse(buffer);
    if (root != NULL) {
        const cJSON *stored = cJSON_GetObjectItemCaseSensitive(root, "openweathermap_key");
        if (cJSON_IsString(stored) && weather_key_valid(stored->valuestring) &&
            strlen(stored->valuestring) < size) {
            strcpy(key, stored->valuestring);
            weather_secure_zero(stored->valuestring, strlen(stored->valuestring));
        } else {
            ESP_LOGW(TAG, "weather.json holds no usable key; treating as absent");
        }
        cJSON_Delete(root);
    } else {
        ESP_LOGW(TAG, "weather.json is not JSON; treating as absent");
    }
    weather_secure_zero(buffer, WEATHER_KEY_JSON_MAX);
    free(buffer);
}

static esp_err_t weather_key_write(const char *key)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return ESP_ERR_NO_MEM;
    esp_err_t err = ESP_OK;
    if (cJSON_AddNumberToObject(root, "version", 1) == NULL ||
        cJSON_AddStringToObject(root, "openweathermap_key", key) == NULL) {
        err = ESP_ERR_NO_MEM;
    }
    char *text = err == ESP_OK ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    if (text == NULL) return err == ESP_OK ? ESP_ERR_NO_MEM : err;

    /* Written beside and renamed over, so a power cut leaves the old file or
     * the new one and never half of either. */
    FILE *file = fopen(WEATHER_KEY_TEMP_PATH, "w");
    if (file == NULL) {
        weather_secure_zero(text, strlen(text));
        free(text);
        return ESP_FAIL;
    }
    const size_t length = strlen(text);
    const bool written = fwrite(text, 1U, length, file) == length && fflush(file) == 0 &&
                         fsync(fileno(file)) == 0;
    fclose(file);
    weather_secure_zero(text, length);
    free(text);
    if (!written || rename(WEATHER_KEY_TEMP_PATH, WEATHER_KEY_PATH) != 0) {
        unlink(WEATHER_KEY_TEMP_PATH);
        return ESP_FAIL;
    }
    return ESP_OK;
}

bool weather_key_is_set(void)
{
    if (s_lock == NULL) return false;
    lock();
    const bool set = s_config.key[0] != '\0';
    unlock();
    return set;
}

static void weather_wake(void)
{
    if (s_task != NULL) xTaskNotifyGive(s_task);
}

esp_err_t weather_key_save(const char *key)
{
    if (s_lock == NULL) return ESP_ERR_INVALID_STATE;
    const bool clearing = key == NULL || key[0] == '\0';
    if (!clearing && !weather_key_valid(key)) return ESP_ERR_INVALID_ARG;
    esp_err_t err = ESP_OK;
    if (clearing) {
        if (unlink(WEATHER_KEY_PATH) != 0 && access(WEATHER_KEY_PATH, F_OK) == 0) err = ESP_FAIL;
    } else {
        err = weather_key_write(key);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cannot %s the key file", clearing ? "remove" : "write");
        return err;
    }
    lock();
    weather_secure_zero(s_config.key, sizeof(s_config.key));
    if (!clearing) strcpy(s_config.key, key);
    /* The state the page reads is put right here rather than left for the
     * task: "no key" with a key just saved is what it would say until the
     * round ran, and the answer to the save is sent before that. */
    if (s_config.provider == DEVICE_WEATHER_OPENWEATHERMAP) {
        s_status.state = clearing ? WEATHER_STATE_NO_KEY : WEATHER_STATE_WAITING;
        s_status.http_status = 0;
        s_status.report = (weather_report_t){0};
    }
    unlock();
    ESP_LOGI(TAG, "OpenWeatherMap key %s", clearing ? "removed" : "saved");
    weather_wake();
    return ESP_OK;
}

/* --- the fetch --------------------------------------------------------- */

/* One request. Returns the HTTP status, or 0 when the request never reached
 * the point of having one - not the esp_err_t, whose ESP_FAIL is -1 and would
 * read as "the service answered 1" once negated. */
static int weather_fetch(const char *url, char *response, size_t response_size, size_t *length)
{
    *length = 0U;
    const esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = WEATHER_HTTP_TIMEOUT_MS,
        /* wttr.in answers a browser with a page and everyone else with
         * text; with `format=` in the query it answers text regardless, but
         * the name is still worth giving. */
        .user_agent = "jRadio/1.0",
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) return 0;
    esp_err_t err = esp_http_client_open(client, 0);
    if (err == ESP_OK && esp_http_client_fetch_headers(client) < 0) err = ESP_FAIL;
    int status = 0;
    if (err == ESP_OK) {
        status = esp_http_client_get_status_code(client);
        /* Reads to the end whatever the framing: Open-Meteo answers chunked
         * with no Content-Length, and this is what handles that. */
        const int read = esp_http_client_read_response(client, response, (int)response_size - 1);
        if (read < 0) {
            err = ESP_FAIL;
        } else {
            response[read] = '\0';
            *length = (size_t)read;
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "GET %.48s failed: %s", url, esp_err_to_name(err));
        return 0;
    }
    return status;
}

static bool weather_parse(device_weather_provider_t provider, const char *body, size_t length,
                          weather_report_t *report)
{
    switch (provider) {
    case DEVICE_WEATHER_OPEN_METEO: return weather_parse_open_meteo(body, length, report);
    case DEVICE_WEATHER_WTTR: return weather_parse_wttr(body, length, report);
    case DEVICE_WEATHER_OPENWEATHERMAP:
        return weather_parse_openweathermap(body, length, report);
    case DEVICE_WEATHER_OFF: break;
    }
    return false;
}

static const char *provider_name(device_weather_provider_t provider)
{
    switch (provider) {
    case DEVICE_WEATHER_OPEN_METEO: return "Open-Meteo";
    case DEVICE_WEATHER_WTTR: return "wttr.in";
    case DEVICE_WEATHER_OPENWEATHERMAP: return "OpenWeatherMap";
    case DEVICE_WEATHER_OFF: break;
    }
    return "off";
}

static void set_state(weather_state_t state, int http_status)
{
    lock();
    s_status.state = state;
    s_status.http_status = http_status;
    unlock();
}

/* One round: returns how long to sleep before the next. */
static uint32_t weather_round(char *response)
{
    weather_config_t config;
    lock();
    config = s_config;
    unlock();

    char url[WEATHER_URL_MAX];
    if (config.provider == DEVICE_WEATHER_OFF) {
        set_state(WEATHER_STATE_OFF, 0);
        weather_secure_zero(&config, sizeof(config));
        return portMAX_DELAY;
    }
    const size_t url_length = weather_request_url(url, sizeof(url), config.provider,
                                                  config.latitude, config.longitude, config.key);
    if (url_length == 0U) {
        /* The only way a provider that is on has no request is a missing
         * key: the coordinates always have a default. */
        set_state(WEATHER_STATE_NO_KEY, 0);
        weather_secure_zero(&config, sizeof(config));
        return portMAX_DELAY;
    }
    if (wifi_provisioning_status().mode != WIFI_PROVISIONING_STA_CONNECTED) {
        weather_secure_zero(&config, sizeof(config));
        weather_secure_zero(url, sizeof(url));
        return WEATHER_NO_NETWORK_MS;
    }

    size_t length = 0U;
    const int status = weather_fetch(url, response, WEATHER_RESPONSE_MAX, &length);
    weather_secure_zero(url, sizeof(url));
    if (!s_stack_reported) {
        s_stack_reported = true;
        ESP_LOGI(TAG, "first fetch done, stack headroom %u",
                 (unsigned int)uxTaskGetStackHighWaterMark(NULL));
    }

    weather_report_t report;
    const bool ok = status == 200 && weather_parse(config.provider, response, length, &report);
    if (ok) {
        char text[16];
        weather_temperature_text(text, sizeof(text), &report);
        lock();
        s_status.state = WEATHER_STATE_OK;
        s_status.http_status = status;
        s_status.report = report;
        s_report_at_ms = now_ms();
        unlock();
        ESP_LOGI(TAG, "%s: %s, %s", provider_name(config.provider), text,
                 weather_icon_name(report.icon));
        weather_secure_zero(&config, sizeof(config));
        return WEATHER_POLL_MS;
    }

    if (status == 200) {
        ESP_LOGW(TAG, "%s answered 200 with something that is not weather: %.60s",
                 provider_name(config.provider), response);
    } else if (status > 0) {
        ESP_LOGW(TAG, "%s answered %d", provider_name(config.provider), status);
    }
    lock();
    const uint32_t failures = s_status.state == WEATHER_STATE_FAILED ? 1U : 0U;
    s_status.state = WEATHER_STATE_FAILED;
    s_status.http_status = status;
    unlock();
    weather_secure_zero(&config, sizeof(config));
    /* A key the service refused will be refused again in a minute: wait the
     * full interval so the account is not flagged for hammering. */
    if (status == 401 || status == 403) return WEATHER_POLL_MS;
    return failures ? WEATHER_RETRY_MIN_MS * 4U : WEATHER_RETRY_MIN_MS;
}

static void weather_task(void *argument)
{
    (void)argument;
    char *response = malloc(WEATHER_RESPONSE_MAX);
    if (response == NULL) {
        ESP_LOGE(TAG, "no memory for a response buffer; weather off");
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    uint32_t delay_ms = 0U;
    for (;;) {
        /* Woken early by a settings change or a saved key; the notification
         * count is cleared on the way out, so several changes are one round. */
        if (delay_ms > 0U) {
            const TickType_t ticks =
                delay_ms == portMAX_DELAY ? portMAX_DELAY : pdMS_TO_TICKS(delay_ms);
            (void)ulTaskNotifyTake(pdTRUE, ticks);
        }
        delay_ms = weather_round(response);
    }
}

/* --- the public surface ------------------------------------------------ */

static void weather_take_settings(const device_settings_t *settings)
{
    lock();
    s_config.provider = settings->weather_provider;
    snprintf(s_config.latitude, sizeof(s_config.latitude), "%s", settings->weather_latitude);
    snprintf(s_config.longitude, sizeof(s_config.longitude), "%s", settings->weather_longitude);
    unlock();
}

esp_err_t weather_init(const device_settings_t *settings)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) return ESP_ERR_NO_MEM;
    }
    /* The key file lives on the same mount as wifi.json; asking for the mount
     * is idempotent and is what makes this callable before Wi-Fi. */
    if (wifi_settings_storage_init() == ESP_OK) {
        char key[WEATHER_KEY_MAX + 1];
        weather_key_load(key, sizeof(key));
        lock();
        strcpy(s_config.key, key);
        unlock();
        weather_secure_zero(key, sizeof(key));
    }
    if (settings != NULL) weather_take_settings(settings);
    if (s_task == NULL) {
        /* Core 0 with Wi-Fi and lwIP, like every other network task here;
         * core 1 belongs to the decoder. */
        if (xTaskCreatePinnedToCore(weather_task, "weather", WEATHER_TASK_STACK, NULL,
                                    WEATHER_TASK_PRIORITY, &s_task, 0) != pdPASS) {
            s_task = NULL;
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

void weather_apply(const device_settings_t *settings)
{
    if (s_lock == NULL || settings == NULL) return;
    lock();
    const bool changed = s_config.provider != settings->weather_provider ||
                         strcmp(s_config.latitude, settings->weather_latitude) != 0 ||
                         strcmp(s_config.longitude, settings->weather_longitude) != 0;
    unlock();
    if (!changed) return;
    weather_take_settings(settings);
    /* The old reading is not this place's or this service's: take it down
     * rather than show it under the new name for up to a quarter of an hour. */
    lock();
    s_status.report = (weather_report_t){0};
    /* Said here rather than left for the round, because the answer to the
     * change is sent first and the page shows what it says. */
    if (settings->weather_provider == DEVICE_WEATHER_OFF) {
        s_status.state = WEATHER_STATE_OFF;
    } else if (settings->weather_provider == DEVICE_WEATHER_OPENWEATHERMAP &&
               s_config.key[0] == '\0') {
        s_status.state = WEATHER_STATE_NO_KEY;
    } else {
        s_status.state = WEATHER_STATE_WAITING;
    }
    s_status.http_status = 0;
    unlock();
    weather_wake();
}

bool weather_current(weather_report_t *report)
{
    if (report == NULL) return false;
    *report = (weather_report_t){0};
    if (s_lock == NULL) return false;
    lock();
    const bool have = s_status.report.valid && s_config.provider != DEVICE_WEATHER_OFF &&
                      now_ms() - s_report_at_ms < (int64_t)WEATHER_STALE_MS;
    if (have) *report = s_status.report;
    unlock();
    return have;
}

void weather_status(weather_status_t *status)
{
    if (status == NULL) return;
    *status = (weather_status_t){0};
    if (s_lock == NULL) return;
    lock();
    *status = s_status;
    if (now_ms() - s_report_at_ms >= (int64_t)WEATHER_STALE_MS) status->report.valid = false;
    unlock();
}

#else

/* Host build: the pure half is in weather_report.c, and nothing here is
 * testable without a network. */
typedef int weather_device_only_t;

#endif
