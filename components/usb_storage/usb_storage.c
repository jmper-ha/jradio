#include <dirent.h>
#include <stdlib.h>
#include "esp_log.h"
#include "usb/usb_host.h"
#include "usb/msc_host_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "usb_storage.h"

static const char *TAG = "usb";
typedef enum { USB_MSG_CONNECT, USB_MSG_DISCONNECT } usb_msg_id_t;
typedef struct { usb_msg_id_t id; union { uint8_t address; msc_host_device_handle_t handle; }; } usb_msg_t;
static QueueHandle_t s_queue;
static EventGroupHandle_t s_events;
#define USB_HOST_READY_BIT BIT0
static msc_host_device_handle_t s_device;
static msc_host_vfs_handle_t s_vfs;

static void msc_event(const msc_host_event_t *event, void *arg)
{
    (void)arg;
    usb_msg_t msg = {0};
    if (event->event == MSC_DEVICE_CONNECTED) { msg.id = USB_MSG_CONNECT; msg.address = event->device.address; }
    else if (event->event == MSC_DEVICE_DISCONNECTED) { msg.id = USB_MSG_DISCONNECT; msg.handle = event->device.handle; }
    else { return; }
    if (xQueueSend(s_queue, &msg, 0) != pdTRUE) ESP_LOGW(TAG, "USB event queue full");
}

static void list_root(void)
{
    DIR *dir = opendir("/usb0");
    if (!dir) { ESP_LOGE(TAG, "cannot open /usb0"); return; }
    ESP_LOGI(TAG, "files on /usb0:");
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) ESP_LOGI(TAG, "%s", entry->d_name);
    closedir(dir);
}

static void usb_lib_task(void *arg)
{
    (void)arg;
    ESP_ERROR_CHECK(usb_host_install(&(usb_host_config_t){ .intr_flags = ESP_INTR_FLAG_LEVEL1 }));
    xEventGroupSetBits(s_events, USB_HOST_READY_BIT);
    for (;;) { uint32_t flags; usb_host_lib_handle_events(portMAX_DELAY, &flags); }
}

static void usb_app_task(void *arg)
{
    (void)arg;
    xEventGroupWaitBits(s_events, USB_HOST_READY_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    const msc_host_driver_config_t config = { .create_backround_task = true, .task_priority = 5, .stack_size = 4096, .callback = msc_event };
    ESP_ERROR_CHECK(msc_host_install(&config));
    ESP_LOGI(TAG, "USB Host ready; waiting for a FAT USB flash drive");
    for (;;) {
        usb_msg_t msg;
        xQueueReceive(s_queue, &msg, portMAX_DELAY);
        if (msg.id == USB_MSG_CONNECT && s_device == NULL) {
            esp_err_t err = msc_host_install_device(msg.address, &s_device);
            if (err == ESP_OK) {
                const esp_vfs_fat_mount_config_t mount = { .format_if_mount_failed = false, .max_files = 8, .allocation_unit_size = 8192 };
                err = msc_host_vfs_register(s_device, "/usb0", &mount, &s_vfs);
            }
            if (err == ESP_OK) { ESP_LOGI(TAG, "USB storage mounted at /usb0"); list_root(); }
            else { ESP_LOGE(TAG, "USB mount failed: %s", esp_err_to_name(err)); if (s_device) { msc_host_uninstall_device(s_device); s_device = NULL; } }
        } else if (msg.id == USB_MSG_DISCONNECT && msg.handle == s_device) {
            ESP_LOGI(TAG, "USB storage disconnected");
            if (s_vfs) { msc_host_vfs_unregister(s_vfs); s_vfs = NULL; }
            msc_host_uninstall_device(s_device); s_device = NULL;
        }
    }
}

esp_err_t usb_storage_init(void)
{
    esp_log_level_set("USB_MSC", ESP_LOG_DEBUG);
    esp_log_level_set("ENUM", ESP_LOG_DEBUG);
    esp_log_level_set("USBH", ESP_LOG_DEBUG);
    esp_log_level_set("HCD DWC", ESP_LOG_DEBUG);
    esp_log_level_set("HUB", ESP_LOG_DEBUG);
    esp_log_level_set("USB HOST", ESP_LOG_DEBUG);
    esp_log_level_set("usb_phy", ESP_LOG_DEBUG);
    s_queue = xQueueCreate(4, sizeof(usb_msg_t));
    if (!s_queue) return ESP_ERR_NO_MEM;
    s_events = xEventGroupCreate();
    if (!s_events) { vQueueDelete(s_queue); s_queue = NULL; return ESP_ERR_NO_MEM; }
    if (xTaskCreate(usb_lib_task, "usb_lib", 4096, NULL, 3, NULL) != pdPASS ||
        xTaskCreate(usb_app_task, "usb_msc", 4096, NULL, 4, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}
