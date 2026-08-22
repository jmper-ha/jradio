#include <stdint.h>
#include "esp_log.h"
#include "usb/usb_host.h"
#include "usb/msc_host_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "file_browser.h"
#include "file_storage.h"
#include "usb_storage.h"

static const char *TAG = "usb";
typedef enum { USB_MSG_CONNECT, USB_MSG_DISCONNECT } usb_msg_id_t;
typedef struct { usb_msg_id_t id; union { uint8_t address; msc_host_device_handle_t handle; }; } usb_msg_t;
static QueueHandle_t s_queue;
static EventGroupHandle_t s_events;
#define USB_HOST_READY_BIT BIT0
#define USB_HOST_FAILED_BIT BIT1
static msc_host_device_handle_t s_device;
static msc_host_vfs_handle_t s_vfs;

static volatile file_browser_media_t s_media = FILE_BROWSER_MEDIA_ABSENT;
static usb_storage_media_removing_cb_t s_media_removing_callback;

// How long to wait for a drive to announce itself before assuming it is a
// stale one left plugged in across a reset, and how many times to try.
#define USB_STORAGE_RECOVERY_DELAY_MS 3000U
#define USB_STORAGE_RECOVERY_SETTLE_MS 200U
#define USB_STORAGE_RECOVERY_ATTEMPTS 3U

static void msc_event(const msc_host_event_t *event, void *arg)
{
    (void)arg;
    usb_msg_t msg = {0};
    if (event->event == MSC_DEVICE_CONNECTED) { msg.id = USB_MSG_CONNECT; msg.address = event->device.address; }
    else if (event->event == MSC_DEVICE_DISCONNECTED) { msg.id = USB_MSG_DISCONNECT; msg.handle = event->device.handle; }
    else { return; }
    if (xQueueSend(s_queue, &msg, 0) != pdTRUE) ESP_LOGW(TAG, "USB event queue full");
}

bool usb_storage_is_mounted(void)
{
    return s_media == FILE_BROWSER_MEDIA_READY;
}

file_browser_media_t usb_storage_media(void)
{
    return s_media;
}

static void usb_lib_task(void *arg)
{
    (void)arg;
    const esp_err_t err = usb_host_install(
        &(usb_host_config_t){.intr_flags = ESP_INTR_FLAG_LEVEL1});
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "USB host install failed: %s", esp_err_to_name(err));
        xEventGroupSetBits(s_events, USB_HOST_FAILED_BIT);
        vTaskDelete(NULL);
        return;
    }
    xEventGroupSetBits(s_events, USB_HOST_READY_BIT);
    for (;;) {
        uint32_t flags = 0;
        const esp_err_t event_result = usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (event_result != ESP_OK) {
            ESP_LOGE(TAG, "USB host event handling failed: %s",
                     esp_err_to_name(event_result));
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

/* A drive left plugged in across an ESP32 reset never sees a power cycle: its
 * VBUS comes from a separate supply, so it keeps the address the previous
 * firmware gave it and ignores anything addressed to 0, which is where a fresh
 * host starts. Nothing announces this - the drive simply never appears.
 *
 * Toggling the root port's power fixes it without switching VBUS at all. The
 * call only clears and sets the PRTPWR bit of the DWC OTG host port register,
 * and on re-power a device still holding its D+ pull-up raises a connection
 * event, to which the hub driver answers with a real bus reset on D+/D-. That
 * reset returns the drive to the Default state at address 0, and enumeration
 * starts over. */
static esp_err_t usb_storage_recover_port(void)
{
    esp_err_t err = usb_host_lib_set_root_port_power(false);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "root port power off failed: %s", esp_err_to_name(err));
        return err;
    }
    // Powering off raises a disconnect that the host event task has to consume
    // first: hcd_port_command() refuses to act while a port event is pending,
    // so powering back on without this gap fails with ESP_ERR_INVALID_STATE.
    vTaskDelay(pdMS_TO_TICKS(USB_STORAGE_RECOVERY_SETTLE_MS));
    err = usb_host_lib_set_root_port_power(true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "root port power on failed: %s", esp_err_to_name(err));
    }
    return err;
}

static void usb_app_task(void *arg)
{
    (void)arg;
    const EventBits_t startup = xEventGroupWaitBits(
        s_events, USB_HOST_READY_BIT | USB_HOST_FAILED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    if ((startup & USB_HOST_FAILED_BIT) != 0U) {
        ESP_LOGW(TAG, "USB storage disabled because host initialization failed");
        vTaskDelete(NULL);
        return;
    }
    const msc_host_driver_config_t config = { .create_backround_task = true, .task_priority = 5, .stack_size = 4096, .callback = msc_event };
    const esp_err_t install_result = msc_host_install(&config);
    if (install_result != ESP_OK) {
        ESP_LOGE(TAG, "USB MSC install failed: %s", esp_err_to_name(install_result));
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "USB Host ready; waiting for a FAT USB flash drive");
    unsigned int recovery_attempts = 0U;
    TickType_t wait = pdMS_TO_TICKS(USB_STORAGE_RECOVERY_DELAY_MS);
    for (;;) {
        usb_msg_t msg;
        if (xQueueReceive(s_queue, &msg, wait) != pdTRUE) {
            // Silence here is ambiguous: either no drive is plugged in, or one
            // is plugged in and stuck from before the reset. Re-enumerating
            // costs nothing in the first case and is the only cure in the
            // second, so try it a few times and then stop waking up.
            if (s_device == NULL && recovery_attempts < USB_STORAGE_RECOVERY_ATTEMPTS) {
                ++recovery_attempts;
                ESP_LOGI(TAG, "no drive yet; re-enumerating the root port (%u/%u)",
                         recovery_attempts, (unsigned int)USB_STORAGE_RECOVERY_ATTEMPTS);
                (void)usb_storage_recover_port();
            } else {
                // A drive inserted later announces itself through the callback,
                // so from here on there is nothing to poll for.
                wait = portMAX_DELAY;
            }
            continue;
        }
        if (msg.id == USB_MSG_CONNECT && s_device == NULL) {
            esp_err_t err = msc_host_install_device(msg.address, &s_device);
            if (err == ESP_OK) {
                const esp_vfs_fat_mount_config_t mount = { .format_if_mount_failed = false, .max_files = 8, .allocation_unit_size = 8192 };
                err = msc_host_vfs_register(s_device, "/usb0", &mount, &s_vfs);
            }
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "USB storage mounted at %s", USB_STORAGE_ROOT_PATH);
                s_media = FILE_BROWSER_MEDIA_READY;
                // A drive can mount and still fail to read - a damaged FAT, or
                // a filesystem the build has no code page for. That is not the
                // same as no drive, so it keeps its own state.
                if (file_storage_read_directory(USB_STORAGE_ROOT_PATH) == ESP_OK) {
                    file_storage_log_listing();
                } else {
                    ESP_LOGE(TAG, "USB root directory is unreadable");
                    s_media = FILE_BROWSER_MEDIA_UNREADABLE;
                }
            }
            else {
                ESP_LOGE(TAG, "USB mount failed: %s", esp_err_to_name(err));
                // The drive is physically there; it simply will not mount.
                s_media = FILE_BROWSER_MEDIA_UNREADABLE;
                if (s_device) { msc_host_uninstall_device(s_device); s_device = NULL; }
            }
        } else if (msg.id == USB_MSG_DISCONNECT && msg.handle == s_device) {
            ESP_LOGI(TAG, "USB storage disconnected");
            // Clear the listing before tearing the mount down: it names files
            // that are no longer reachable, and the UI polls it independently.
            s_media = FILE_BROWSER_MEDIA_ABSENT;
            if (file_storage_listing_is_on(USB_STORAGE_ROOT_PATH)) {
                file_storage_open_empty(USB_STORAGE_ROOT_PATH);
            }
            // Everything that reads the drive has to be stopped before the
            // mount goes, not after: the player runs on its own task and can
            // be inside fread(), and msc_host_vfs_unregister() frees the FATFS
            // state that read is standing on. This blocks until the player has
            // closed its file.
            if (s_media_removing_callback != NULL) s_media_removing_callback();
            if (s_vfs) { msc_host_vfs_unregister(s_vfs); s_vfs = NULL; }
            msc_host_uninstall_device(s_device); s_device = NULL;
            ESP_LOGI(TAG, "USB storage released");
        }
    }
}

void usb_storage_set_media_removing_callback(usb_storage_media_removing_cb_t callback)
{
    s_media_removing_callback = callback;
}

esp_err_t usb_storage_init(void)
{
    if (s_queue != NULL) {
        return ESP_OK;
    }
    esp_log_level_set("USB_MSC", ESP_LOG_INFO);
    esp_log_level_set("ENUM", ESP_LOG_WARN);
    esp_log_level_set("USBH", ESP_LOG_WARN);
    esp_log_level_set("HCD DWC", ESP_LOG_WARN);
    esp_log_level_set("HUB", ESP_LOG_WARN);
    esp_log_level_set("USB HOST", ESP_LOG_WARN);
    esp_log_level_set("usb_phy", ESP_LOG_WARN);
    s_queue = xQueueCreate(4, sizeof(usb_msg_t));
    if (!s_queue) return ESP_ERR_NO_MEM;
    s_events = xEventGroupCreate();
    if (!s_events) { vQueueDelete(s_queue); s_queue = NULL; return ESP_ERR_NO_MEM; }
    const esp_err_t listing = file_storage_init();
    if (listing != ESP_OK) {
        vEventGroupDelete(s_events);
        vQueueDelete(s_queue);
        s_events = NULL;
        s_queue = NULL;
        return listing;
    }
    // Registered before the host task starts: a mount can complete at any
    // moment after that, and reading its root asks whether this volume is up.
    (void)file_storage_register_volume(USB_STORAGE_ROOT_PATH, usb_storage_is_mounted);
    TaskHandle_t app_task = NULL;
    // 6144, not 4096: the periodic health report caught this task down to 500
    // bytes of headroom. Mounting the filesystem and listing a directory is
    // the deep part - FATFS, the MSC transfers under it, and a kilobyte path
    // buffer per entry. Moving that buffer off the stack was tried first and
    // returned only 80 bytes while costing a kilobyte of .bss, so the stack is
    // simply the right size for the work.
    if (xTaskCreate(usb_app_task, "usb_msc", 6144, NULL, 4, &app_task) != pdPASS) {
        vEventGroupDelete(s_events);
        vQueueDelete(s_queue);
        s_events = NULL;
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(usb_lib_task, "usb_lib", 4096, NULL, 3, NULL) != pdPASS) {
        vTaskDelete(app_task);
        vEventGroupDelete(s_events);
        vQueueDelete(s_queue);
        s_events = NULL;
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
