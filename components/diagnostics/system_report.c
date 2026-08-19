#include "system_report.h"

#include "system_health.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"

#include <stdio.h>

static const char *TAG = "health";

/* A minute. Short enough that a slow leak or a stack creeping down is visible
 * within one listening session, long enough that the line does not crowd out
 * the audio reports that share this log. */
#define SYSTEM_REPORT_INTERVAL_MS 60000U

/* The tasks worth watching: everything this firmware creates, plus the HTTP
 * server, whose stack runs our request handlers. Looked up by name, so the
 * ones that only exist while something is playing cost nothing when it is not.
 *
 * Listed here rather than discovered because enumerating tasks needs
 * CONFIG_FREERTOS_USE_TRACE_FACILITY, which is off - and a fixed list also
 * keeps idle and system tasks out of a report meant to be read at a glance. */
static const char *const s_watched_tasks[] = {
    "player_control", "ui",       "radio_decode", "usb_play", "usb_msc",
    "usb_lib",        "input_log", "board_input", "httpd",    "wifi_reconnect",
};

static system_health_t s_health;
static uint32_t s_next_report_ms;
static bool s_started;

static const char *reset_reason_name(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external pin";
    case ESP_RST_SW: return "software restart";
    case ESP_RST_PANIC: return "PANIC or assertion";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT: return "other watchdog";
    case ESP_RST_DEEPSLEEP: return "deep sleep wake";
    case ESP_RST_BROWNOUT: return "BROWNOUT - check the supply";
    case ESP_RST_SDIO: return "SDIO";
    default: return "unknown";
    }
}

void system_report_boot(void)
{
    system_health_init(&s_health);
    s_next_report_ms = 0U;
    s_started = false;

    const esp_reset_reason_t reason = esp_reset_reason();
    /* A panic or a watchdog is the device telling you the previous run ended
     * badly, and it is the only place that survives the reboot - so it is a
     * warning, not an info line, and it names the cause in words rather than
     * leaving a number to look up. */
    if (reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT ||
        reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT ||
        reason == ESP_RST_BROWNOUT) {
        ESP_LOGW(TAG, "previous run ended in a reset: %s", reset_reason_name(reason));
    } else {
        ESP_LOGI(TAG, "reset reason: %s", reset_reason_name(reason));
    }

    ESP_LOGI(TAG, "boot heap: internal_free=%u largest=%u dma_largest=%u psram_free=%u",
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                            MALLOC_CAP_8BIT),
             (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

void system_report_tick(uint32_t now_ms)
{
    if (!s_started) {
        /* Anchored to the first call rather than to zero, so the first report
         * comes a full interval after the device is up instead of immediately,
         * while allocation is still settling. */
        s_started = true;
        s_next_report_ms = now_ms + SYSTEM_REPORT_INTERVAL_MS;
        return;
    }
    if ((int32_t)(now_ms - s_next_report_ms) < 0) return;
    s_next_report_ms = now_ms + SYSTEM_REPORT_INTERVAL_MS;

    for (size_t index = 0U; index < sizeof(s_watched_tasks) / sizeof(s_watched_tasks[0]);
         ++index) {
        const TaskHandle_t handle = xTaskGetHandle(s_watched_tasks[index]);
        if (handle == NULL) continue;
        /* ESP-IDF returns this in bytes, unlike upstream FreeRTOS, which
         * returns words. */
        const uint32_t free_bytes = (uint32_t)uxTaskGetStackHighWaterMark(handle);
        if (system_health_note_stack(&s_health, s_watched_tasks[index], free_bytes)) {
            ESP_LOGW(TAG, "task %s has only %u bytes of stack left",
                     s_watched_tasks[index], (unsigned int)free_bytes);
        }
    }

    const uint32_t internal_free =
        (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t internal_largest =
        (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (system_health_note_heap(&s_health, internal_free, internal_largest)) {
        ESP_LOGW(TAG, "largest internal block down to %u bytes; TLS needs a "
                      "contiguous DMA-capable one",
                 (unsigned int)internal_largest);
    }

    ESP_LOGI(TAG,
             "uptime=%us internal_free=%u/min %u largest=%u/min %u dma_largest=%u "
             "psram_free=%u",
             (unsigned int)(now_ms / 1000U), (unsigned int)internal_free,
             (unsigned int)s_health.internal_min_free, (unsigned int)internal_largest,
             (unsigned int)s_health.internal_min_largest,
             (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    /* Every task, not just the tightest: which one is closest to the edge
     * changes as the device is used, and the whole table is what tells a stack
     * that is creeping down apart from one that has simply always been low.
     *
     * The buffer is static because this runs on the UI task, which is the
     * tightest stack here - putting a few hundred bytes on it to report on
     * stack space would be its own joke. Only ever called from that one task. */
    static char stacks[224];
    size_t used = 0U;
    for (size_t index = 0U; index < s_health.task_count && used < sizeof(stacks); ++index) {
        const int written =
            snprintf(stacks + used, sizeof(stacks) - used, "%s%s=%u",
                     used == 0U ? "" : " ", s_health.tasks[index].name,
                     (unsigned int)s_health.tasks[index].stack_min_free);
        /* snprintf reports what it wanted to write, not what it did, so an
         * unchecked add would walk `used` past the buffer and make the next
         * size argument underflow. */
        if (written < 0 || (size_t)written >= sizeof(stacks) - used) break;
        used += (size_t)written;
    }
    ESP_LOGI(TAG, "stack headroom, least seen: %s", stacks);
}
