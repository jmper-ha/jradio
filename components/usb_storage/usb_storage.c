#include <dirent.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "usb/usb_host.h"
#include "usb/msc_host_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "usb_browser.h"
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

// One directory is on screen at a time, so one shared listing is enough. At
// 264 bytes per entry this is ~68 KB, which is why it lives in PSRAM.
#define USB_STORAGE_MAX_ENTRIES 256

static usb_browser_entry_t *s_entries;
static usb_browser_dir_t s_listing;
static SemaphoreHandle_t s_listing_lock;
static volatile usb_browser_media_t s_media = USB_BROWSER_MEDIA_ABSENT;
static usb_storage_media_removing_cb_t s_media_removing_callback;

// How long to wait for a drive to announce itself before assuming it is a
// stale one left plugged in across a reset, and how many times to try.
#define USB_STORAGE_RECOVERY_DELAY_MS 3000U
#define USB_STORAGE_RECOVERY_SETTLE_MS 200U
#define USB_STORAGE_RECOVERY_ATTEMPTS 3U

static bool listing_lock(void)
{
    return s_listing_lock != NULL &&
           xSemaphoreTake(s_listing_lock, pdMS_TO_TICKS(1000)) == pdTRUE;
}

static void listing_unlock(void)
{
    if (s_listing_lock != NULL) xSemaphoreGive(s_listing_lock);
}

static void msc_event(const msc_host_event_t *event, void *arg)
{
    (void)arg;
    usb_msg_t msg = {0};
    if (event->event == MSC_DEVICE_CONNECTED) { msg.id = USB_MSG_CONNECT; msg.address = event->device.address; }
    else if (event->event == MSC_DEVICE_DISCONNECTED) { msg.id = USB_MSG_DISCONNECT; msg.handle = event->device.handle; }
    else { return; }
    if (xQueueSend(s_queue, &msg, 0) != pdTRUE) ESP_LOGW(TAG, "USB event queue full");
}

static usb_browser_entry_kind_t entry_kind(const char *path, const struct dirent *entry)
{
    // FATFS fills d_type, but fall back to stat() rather than trusting it: a
    // directory misread as a file becomes an unplayable "track", and one
    // misread the other way hides everything below it.
    if (entry->d_type == DT_DIR) return USB_BROWSER_ENTRY_DIRECTORY;
    if (entry->d_type == DT_REG) return USB_BROWSER_ENTRY_FILE;
    char child[USB_BROWSER_PATH_MAX_LEN];
    struct stat info;
    if (usb_browser_path_child(path, entry->d_name, child, sizeof(child)) &&
        stat(child, &info) == 0 && S_ISDIR(info.st_mode)) {
        return USB_BROWSER_ENTRY_DIRECTORY;
    }
    return USB_BROWSER_ENTRY_FILE;
}

esp_err_t usb_storage_read_directory(const char *path)
{
    if (path == NULL || s_entries == NULL) return ESP_ERR_INVALID_ARG;
    // READY only: the first read after a mount runs while the state is still
    // being decided, so this deliberately accepts anything but "no drive".
    if (s_media == USB_BROWSER_MEDIA_ABSENT) return ESP_ERR_INVALID_STATE;
    DIR *dir = opendir(path);
    if (dir == NULL) {
        ESP_LOGE(TAG, "cannot open %s", path);
        return ESP_FAIL;
    }
    if (!listing_lock()) {
        closedir(dir);
        return ESP_ERR_TIMEOUT;
    }
    usb_browser_dir_init(&s_listing, s_entries, USB_STORAGE_MAX_ENTRIES, path);
    const struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        usb_browser_dir_add(&s_listing, entry->d_name, entry_kind(path, entry));
    }
    usb_browser_dir_sort(&s_listing);
    const size_t count = s_listing.count;
    const size_t dropped_full = s_listing.dropped_full;
    const size_t dropped_long = s_listing.dropped_long_name;
    listing_unlock();
    closedir(dir);
    if (dropped_full > 0U || dropped_long > 0U) {
        ESP_LOGW(TAG, "%s: %u entries dropped (listing full=%u, name too long=%u)", path,
                 (unsigned)(dropped_full + dropped_long), (unsigned)dropped_full,
                 (unsigned)dropped_long);
    }
    ESP_LOGI(TAG, "%s: %u entries", path, (unsigned)count);
    return ESP_OK;
}

bool usb_storage_is_mounted(void)
{
    return s_media == USB_BROWSER_MEDIA_READY;
}

usb_browser_media_t usb_storage_media(void)
{
    return s_media;
}

size_t usb_storage_entry_count(void)
{
    if (!listing_lock()) return 0U;
    const size_t count = s_listing.count;
    listing_unlock();
    return count;
}

bool usb_storage_entry_at(size_t index, usb_browser_entry_t *out)
{
    if (out == NULL || !listing_lock()) return false;
    const usb_browser_entry_t *entry = usb_browser_dir_entry(&s_listing, index);
    if (entry != NULL) *out = *entry;
    listing_unlock();
    return entry != NULL;
}

size_t usb_storage_find_entry(const char *name)
{
    // Same "past the end on failure" contract as next_file, and for the same
    // reason: the caller falls back to the browser rather than opening
    // whatever happens to sit at index 0.
    if (!listing_lock()) return SIZE_MAX;
    const size_t index = usb_browser_dir_find(&s_listing, name);
    listing_unlock();
    return index;
}

size_t usb_storage_next_file(size_t from)
{
    // SIZE_MAX, not 0: the caller advances to whatever comes back and only
    // stops when it is past the end, so returning 0 here would restart the
    // directory from its first track instead of stopping. Failing "past the
    // end" makes a lock timeout indistinguishable from "nothing left", which
    // is the safe direction to be wrong in.
    if (!listing_lock()) return SIZE_MAX;
    const size_t index = usb_browser_dir_next_file(&s_listing, from);
    listing_unlock();
    return index;
}

bool usb_storage_current_path(char *out, size_t out_size)
{
    if (out == NULL || out_size == 0U || !listing_lock()) return false;
    const size_t length = strlen(s_listing.path);
    const bool fits = length < out_size;
    if (fits) memcpy(out, s_listing.path, length + 1U);
    listing_unlock();
    return fits;
}

/* Copies each entry out before printing it. Holding the lock across the whole
 * loop meant holding it across the UART: a 256-entry directory is some 15 KB of
 * log, over a second at 115200 baud, which is longer than the 1 s other callers
 * wait for the lock. Track advance ran into exactly that. */
static void log_listing(void)
{
    char path[USB_BROWSER_PATH_MAX_LEN];
    if (!usb_storage_current_path(path, sizeof(path))) return;
    ESP_LOGI(TAG, "files on %s:", path);
    const size_t count = usb_storage_entry_count();
    for (size_t i = 0U; i < count; ++i) {
        usb_browser_entry_t entry;
        if (!usb_storage_entry_at(i, &entry)) break;
        if (entry.kind == USB_BROWSER_ENTRY_DIRECTORY) {
            ESP_LOGI(TAG, "  [dir] %s", entry.name);
        } else {
            ESP_LOGI(TAG, "  %-4s %s", usb_browser_format_name(entry.format), entry.name);
        }
    }
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
                ESP_LOGI(TAG, "USB storage mounted at %s", USB_BROWSER_ROOT_PATH);
                s_media = USB_BROWSER_MEDIA_READY;
                // A drive can mount and still fail to read - a damaged FAT, or
                // a filesystem the build has no code page for. That is not the
                // same as no drive, so it keeps its own state.
                if (usb_storage_read_directory(USB_BROWSER_ROOT_PATH) == ESP_OK) {
                    log_listing();
                } else {
                    ESP_LOGE(TAG, "USB root directory is unreadable");
                    s_media = USB_BROWSER_MEDIA_UNREADABLE;
                }
            }
            else {
                ESP_LOGE(TAG, "USB mount failed: %s", esp_err_to_name(err));
                // The drive is physically there; it simply will not mount.
                s_media = USB_BROWSER_MEDIA_UNREADABLE;
                if (s_device) { msc_host_uninstall_device(s_device); s_device = NULL; }
            }
        } else if (msg.id == USB_MSG_DISCONNECT && msg.handle == s_device) {
            ESP_LOGI(TAG, "USB storage disconnected");
            // Clear the listing before tearing the mount down: it names files
            // that are no longer reachable, and the UI polls it independently.
            s_media = USB_BROWSER_MEDIA_ABSENT;
            if (listing_lock()) {
                usb_browser_dir_init(&s_listing, s_entries, USB_STORAGE_MAX_ENTRIES,
                                     USB_BROWSER_ROOT_PATH);
                listing_unlock();
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
    s_listing_lock = xSemaphoreCreateMutex();
    if (!s_listing_lock) { vEventGroupDelete(s_events); vQueueDelete(s_queue); s_events = NULL; s_queue = NULL; return ESP_ERR_NO_MEM; }
    // Internal SRAM headroom is tight and this block is large, so prefer PSRAM
    // and keep the internal fallback for boards without it.
    const size_t listing_bytes = USB_STORAGE_MAX_ENTRIES * sizeof(usb_browser_entry_t);
    s_entries = heap_caps_malloc(listing_bytes, MALLOC_CAP_SPIRAM);
    if (s_entries == NULL) s_entries = heap_caps_malloc(listing_bytes, MALLOC_CAP_INTERNAL);
    if (s_entries == NULL) {
        vSemaphoreDelete(s_listing_lock);
        vEventGroupDelete(s_events);
        vQueueDelete(s_queue);
        s_listing_lock = NULL;
        s_events = NULL;
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    usb_browser_dir_init(&s_listing, s_entries, USB_STORAGE_MAX_ENTRIES, USB_BROWSER_ROOT_PATH);
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
