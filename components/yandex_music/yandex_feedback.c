#include "yandex_feedback.h"

#include <stdio.h>
#include <string.h>

/* The rotor's own identifiers - station tags, batch ids, track ids - are the
 * only text that ever reaches these two builders, and they are alphanumeric
 * with a handful of punctuation. Anything else means the answer was not what
 * we think it was, and the right response is to file nothing rather than to
 * escape our way into a request the server will read differently. */
static bool yandex_feedback_safe(const char *text, const char *extra)
{
    if (text == NULL) return false;
    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        const char character = *cursor;
        const bool alnum = (character >= '0' && character <= '9') ||
                           (character >= 'a' && character <= 'z') ||
                           (character >= 'A' && character <= 'Z');
        if (!alnum && strchr(extra, character) == NULL) return false;
    }
    return true;
}

static const char *yandex_feedback_type(yandex_feedback_kind_t kind)
{
    switch (kind) {
        case YANDEX_FEEDBACK_RADIO_STARTED: return "radioStarted";
        case YANDEX_FEEDBACK_TRACK_STARTED: return "trackStarted";
        case YANDEX_FEEDBACK_TRACK_FINISHED: return "trackFinished";
        case YANDEX_FEEDBACK_SKIP: return "skip";
    }
    return NULL;
}

size_t yandex_feedback_body(const yandex_feedback_event_t *event, char *out, size_t out_size)
{
    if (event == NULL || out == NULL || out_size == 0U) return 0U;
    out[0] = '\0';
    const char *type = yandex_feedback_type(event->kind);
    if (type == NULL) return 0U;

    int written;
    if (event->kind == YANDEX_FEEDBACK_RADIO_STARTED) {
        /* `from` is optional - the API accepts radioStarted without it - so an
         * unknown or unusable one is left out rather than refused. */
        if (event->from[0] != '\0' && yandex_feedback_safe(event->from, ":-_.")) {
            written = snprintf(out, out_size,
                               "{\"type\":\"%s\",\"timestamp\":%u,\"from\":\"%s\"}", type,
                               (unsigned int)event->timestamp, event->from);
        } else {
            written = snprintf(out, out_size, "{\"type\":\"%s\",\"timestamp\":%u}", type,
                               (unsigned int)event->timestamp);
        }
    } else {
        /* Every other event names a track, and the API refuses one that does
         * not - so an empty id is caught here rather than by a round trip. */
        if (event->track_id[0] == '\0' || !yandex_feedback_safe(event->track_id, ":-_.")) {
            return 0U;
        }
        if (event->kind == YANDEX_FEEDBACK_TRACK_STARTED) {
            written = snprintf(out, out_size,
                               "{\"type\":\"%s\",\"timestamp\":%u,\"trackId\":\"%s\"}", type,
                               (unsigned int)event->timestamp, event->track_id);
        } else {
            written = snprintf(out, out_size,
                               "{\"type\":\"%s\",\"timestamp\":%u,\"trackId\":\"%s\","
                               "\"totalPlayedSeconds\":%u}",
                               type, (unsigned int)event->timestamp, event->track_id,
                               (unsigned int)(event->played_ms / 1000U));
        }
    }
    if (written < 0 || (size_t)written >= out_size) {
        out[0] = '\0';
        return 0U;
    }
    return (size_t)written;
}

size_t yandex_feedback_path(const yandex_feedback_event_t *event, char *out, size_t out_size)
{
    if (event == NULL || out == NULL || out_size == 0U) return 0U;
    out[0] = '\0';
    if (event->station[0] == '\0' || !yandex_feedback_safe(event->station, ":-_.")) return 0U;

    int written;
    /* The batch id is optional - measured: feedback without it is accepted -
     * so one that is missing or oddly shaped costs the event nothing. */
    if (event->batch_id[0] != '\0' && yandex_feedback_safe(event->batch_id, ":-_.")) {
        written = snprintf(out, out_size, "/rotor/station/%s/feedback?batch-id=%s",
                           event->station, event->batch_id);
    } else {
        written = snprintf(out, out_size, "/rotor/station/%s/feedback", event->station);
    }
    if (written < 0 || (size_t)written >= out_size) {
        out[0] = '\0';
        return 0U;
    }
    return (size_t)written;
}

#ifdef ESP_PLATFORM

#include <time.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "yandex_api.h"
#include "yandex_auth.h"

static const char *TAG = "yandex_fb";

/* Two events per track change, and a change happens every few minutes. Six is
 * three changes' worth of backlog, which is more than a transient network
 * failure can consume before the next one replaces it. The queue costs about
 * a kilobyte of internal RAM and is created on the first event, so a device
 * that never opens a Yandex station never pays for it. */
#define YANDEX_FEEDBACK_QUEUE_LEN 6U
/* Measured on the device: 6144 left only 804 bytes free through two POSTs,
 * which is less than the auth task's 1356 on the same size - this path adds
 * the response buffer and the two built strings below to the same TLS
 * handshake. 7168 puts the margin back near 1800; the task logs its own
 * high-water mark, so this is trimmed on evidence rather than on nerve. */
#define YANDEX_FEEDBACK_TASK_STACK 7168
/* Below the decoder and the catalog fetch: nothing here is heard or seen. */
#define YANDEX_FEEDBACK_TASK_PRIORITY 3
/* A track change already costs about 1.7 s of silence while the decode task
 * resolves the next link over three API calls. Sending from underneath it
 * would put a fourth TLS handshake in exactly that window, competing for the
 * internal RAM the handshakes need. So the sender waits until the change is
 * over - nothing here is time-critical, and the timestamp inside the event
 * already carries the moment that mattered. */
#define YANDEX_FEEDBACK_SETTLE_MS 4000
/* The answer measured under 250 bytes; a refusal explains itself at length,
 * and that explanation is the whole value of logging a failure at all. */
#define YANDEX_FEEDBACK_RESPONSE_MAX 768U

static SemaphoreHandle_t s_lock;
static QueueHandle_t s_queue;
static TaskHandle_t s_task;
static UBaseType_t s_stack_low_water = (UBaseType_t)-1;

static void yandex_feedback_send(const yandex_feedback_event_t *event)
{
    char path[128];
    char body[192];
    if (yandex_feedback_path(event, path, sizeof(path)) == 0U ||
        yandex_feedback_body(event, body, sizeof(body)) == 0U) {
        ESP_LOGW(TAG, "event %d could not be addressed", (int)event->kind);
        return;
    }

    char response[YANDEX_FEEDBACK_RESPONSE_MAX];
    int status = 0;
    const esp_err_t err = yandex_api_post_json(path, body, response, sizeof(response), &status);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s did not reach the rotor: %s", body, esp_err_to_name(err));
        return;
    }
    if (status != 200) {
        /* Logged whole: a refusal names the field it could not parse, and that
         * is the only way this contract is ever debugged. */
        ESP_LOGW(TAG, "%s refused with HTTP %d: %.200s", body, status, response);
        return;
    }
    ESP_LOGI(TAG, "%s accepted", body);
}

static void yandex_feedback_task(void *context)
{
    (void)context;
    /* Once, before the first send rather than before each: the wait exists to
     * stay out of the track change, and by the time one event has been sent
     * the change is long over. */
    vTaskDelay(pdMS_TO_TICKS(YANDEX_FEEDBACK_SETTLE_MS));

    while (true) {
        yandex_feedback_event_t event;
        while (xQueueReceive(s_queue, &event, 0) == pdTRUE) {
            yandex_feedback_send(&event);
        }
        /* Deciding to end is done under the lock a poster has to hold to add
         * one, so an event queued at this exact moment either arrives before
         * the check and is drained, or finds s_task cleared and starts a new
         * task. Without that, it could land in a queue nobody is reading. */
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (uxQueueMessagesWaiting(s_queue) > 0U) {
            xSemaphoreGive(s_lock);
            continue;
        }
        s_task = NULL;
        xSemaphoreGive(s_lock);
        break;
    }

    const UBaseType_t headroom = uxTaskGetStackHighWaterMark(NULL);
    if (headroom < s_stack_low_water) {
        s_stack_low_water = headroom;
        ESP_LOGI(TAG, "feedback task stack headroom %u", (unsigned int)headroom);
    }
    vTaskDelete(NULL);
}

esp_err_t yandex_feedback_init(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t yandex_feedback_post(const yandex_feedback_event_t *event)
{
    if (event == NULL) return ESP_ERR_INVALID_ARG;
    if (s_lock == NULL) return ESP_ERR_INVALID_STATE;
    /* An account that is not linked has nothing to report to, and the POST
     * would fail on the missing token a whole TLS handshake later. */
    if (!yandex_auth_is_authorized()) return ESP_ERR_INVALID_STATE;

    yandex_feedback_event_t stamped = *event;
    /* Stamped here rather than by the caller: this is the moment the thing
     * being reported actually happened, and the send is deliberately several
     * seconds - or a network outage - away from it. A clock that has not
     * synchronised yet yields 0, which the API accepts. */
    const time_t now = time(NULL);
    stamped.timestamp = now > 0 ? (uint32_t)now : 0U;

    esp_err_t result = ESP_OK;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_queue == NULL) {
        s_queue = xQueueCreate(YANDEX_FEEDBACK_QUEUE_LEN, sizeof(yandex_feedback_event_t));
    }
    if (s_queue == NULL) {
        result = ESP_ERR_NO_MEM;
    } else if (xQueueSend(s_queue, &stamped, 0) != pdTRUE) {
        ESP_LOGW(TAG, "feedback queue full; dropping a %d event", (int)stamped.kind);
        result = ESP_ERR_NO_MEM;
    } else if (s_task == NULL) {
        TaskHandle_t task = NULL;
        /* Core 0, with Wi-Fi and lwIP - the same reason the catalog fetch is
         * pinned there: core 1 belongs to the decoder, and a TLS handshake on
         * it would take cycles from a stream that is playing.
         *
         * Transient like the catalog task: it drains what is queued and ends,
         * so its 6 KB of stack is held for a second every few minutes rather
         * than for as long as the firmware runs. */
        if (xTaskCreatePinnedToCore(yandex_feedback_task, "yandex_fb",
                                    YANDEX_FEEDBACK_TASK_STACK, NULL,
                                    YANDEX_FEEDBACK_TASK_PRIORITY, &task, 0) != pdPASS) {
            ESP_LOGW(TAG, "no room for the feedback task");
            result = ESP_ERR_NO_MEM;
        } else {
            s_task = task;
        }
    }
    xSemaphoreGive(s_lock);
    return result;
}

#endif
