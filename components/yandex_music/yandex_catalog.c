#include "yandex_catalog.h"

#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "yandex_api.h"
#include "yandex_auth.h"

#define YANDEX_DASHBOARD_PATH "/rotor/stations/dashboard"
/* The answer measured 14268 bytes on a four-station dashboard. Twice that
 * leaves room for a longer list without the truncation warning in yandex_api,
 * and it lives in PSRAM for the length of one fetch. */
#define YANDEX_CATALOG_RESPONSE_MAX (32U * 1024U)
/* TLS handshake, a 14 KB read and the parser, which recurses only as deep as
 * the answer nests. Logged when the task ends so this can be trimmed on
 * evidence, the way the auth task's was. */
#define YANDEX_CATALOG_TASK_STACK 8192
#define YANDEX_CATALOG_TASK_PRIORITY 4

static const char *TAG = "yandex_catalog";

static SemaphoreHandle_t s_lock;
static yandex_catalog_t *s_catalog;
static yandex_catalog_state_t s_state;
static unsigned int s_revision;
static TaskHandle_t s_task;

static void yandex_catalog_lock(void)
{
    if (s_lock != NULL) xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void yandex_catalog_unlock(void)
{
    if (s_lock != NULL) xSemaphoreGive(s_lock);
}

/* PSRAM first with a fallback to internal, the rule everywhere in this
 * firmware: internal SRAM is the scarcest resource on the board. */
static void *yandex_catalog_alloc(size_t size)
{
    void *memory = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (memory == NULL) {
        memory = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return memory;
}

static void yandex_catalog_set_state(yandex_catalog_state_t state)
{
    yandex_catalog_lock();
    s_state = state;
    yandex_catalog_unlock();
}

static void yandex_catalog_task(void *context)
{
    (void)context;
    char *response = yandex_catalog_alloc(YANDEX_CATALOG_RESPONSE_MAX);
    yandex_catalog_t *parsed = response == NULL ? NULL
                                                : yandex_catalog_alloc(sizeof(*parsed));
    if (parsed == NULL) {
        ESP_LOGE(TAG, "no memory for a dashboard fetch");
        yandex_catalog_set_state(YANDEX_CATALOG_FAILED);
    } else {
        int status_code = 0;
        const esp_err_t err = yandex_api_get(YANDEX_DASHBOARD_PATH, response,
                                             YANDEX_CATALOG_RESPONSE_MAX, &status_code);
        if (err != ESP_OK) {
            yandex_catalog_set_state(YANDEX_CATALOG_FAILED);
        } else if (status_code != 200) {
            ESP_LOGE(TAG, "dashboard returned HTTP %d", status_code);
            yandex_catalog_set_state(YANDEX_CATALOG_FAILED);
        } else if (!yandex_catalog_parse_dashboard(response, parsed)) {
            ESP_LOGE(TAG, "dashboard answer could not be read");
            yandex_catalog_set_state(YANDEX_CATALOG_FAILED);
        } else {
            yandex_catalog_lock();
            /* The revision moves only on a real change, so a refresh that
             * found the same stations leaves an open list alone instead of
             * making it blink. */
            const bool changed = !yandex_catalog_equal(s_catalog, parsed);
            if (changed) {
                *s_catalog = *parsed;
                ++s_revision;
            }
            s_state = YANDEX_CATALOG_READY;
            const uint8_t count = s_catalog->count;
            yandex_catalog_unlock();
            ESP_LOGI(TAG, "dashboard: %u stations%s", (unsigned int)count,
                     changed ? "" : " (unchanged)");
        }
    }

    free(response);
    free(parsed);
    yandex_catalog_lock();
    s_task = NULL;
    yandex_catalog_unlock();
    ESP_LOGI(TAG, "catalog task done, stack headroom %u",
             (unsigned int)uxTaskGetStackHighWaterMark(NULL));
    vTaskDelete(NULL);
}

esp_err_t yandex_catalog_init(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) return ESP_ERR_NO_MEM;
    }
    if (s_catalog == NULL) {
        s_catalog = yandex_catalog_alloc(sizeof(*s_catalog));
        if (s_catalog == NULL) return ESP_ERR_NO_MEM;
        *s_catalog = (yandex_catalog_t){0};
    }
    return ESP_OK;
}

esp_err_t yandex_catalog_request_refresh(void)
{
    if (s_lock == NULL || s_catalog == NULL) return ESP_ERR_INVALID_STATE;
    if (!yandex_auth_is_authorized()) return ESP_ERR_INVALID_STATE;

    yandex_catalog_lock();
    const bool busy = s_task != NULL;
    if (!busy) s_state = YANDEX_CATALOG_LOADING;
    yandex_catalog_unlock();
    if (busy) return ESP_OK;

    TaskHandle_t task = NULL;
    /* Core 0, with Wi-Fi and lwIP: core 1 belongs to the decoder, and a TLS
     * handshake there would take cycles from a stream that is playing. */
    if (xTaskCreatePinnedToCore(yandex_catalog_task, "yandex_cat", YANDEX_CATALOG_TASK_STACK,
                                NULL, YANDEX_CATALOG_TASK_PRIORITY, &task, 0) != pdPASS) {
        yandex_catalog_set_state(YANDEX_CATALOG_FAILED);
        return ESP_ERR_NO_MEM;
    }
    yandex_catalog_lock();
    s_task = task;
    yandex_catalog_unlock();
    return ESP_OK;
}

yandex_catalog_state_t yandex_catalog_get_state(void)
{
    yandex_catalog_lock();
    const yandex_catalog_state_t state = s_state;
    yandex_catalog_unlock();
    return state;
}

void yandex_catalog_clear(void)
{
    yandex_catalog_lock();
    if (s_catalog != NULL && s_catalog->count != 0U) {
        *s_catalog = (yandex_catalog_t){0};
        ++s_revision;
    }
    s_state = YANDEX_CATALOG_EMPTY;
    yandex_catalog_unlock();
}

bool yandex_catalog_station_at(size_t index, yandex_station_t *station)
{
    if (station == NULL) return false;
    yandex_catalog_lock();
    const bool present = s_catalog != NULL && index < s_catalog->count;
    if (present) *station = s_catalog->stations[index];
    yandex_catalog_unlock();
    return present;
}

size_t yandex_catalog_count(void)
{
    yandex_catalog_lock();
    const size_t count = s_catalog == NULL ? 0U : s_catalog->count;
    yandex_catalog_unlock();
    return count;
}

unsigned int yandex_catalog_revision(void)
{
    yandex_catalog_lock();
    const unsigned int revision = s_revision;
    yandex_catalog_unlock();
    return revision;
}
