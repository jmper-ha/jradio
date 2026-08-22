#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "file_browser.h"

// Where the drive is mounted. The card is at SD_STORAGE_ROOT_PATH; the two are
// the only FAT volumes the build has room for.
#define USB_STORAGE_ROOT_PATH FILE_BROWSER_USB_ROOT

esp_err_t usb_storage_init(void);

/* Called from the USB host task when a drive is going away, before its
 * filesystem is torn down.
 *
 * The teardown cannot be deferred - msc_host_vfs_unregister() and
 * msc_host_uninstall_device() run the moment this returns - so the callback
 * must not post the work somewhere and come back. It has to have stopped
 * everything that reads from the drive, and closed the files, before it
 * returns; anything still inside fread() when the mount goes is reading freed
 * FATFS state. */
typedef void (*usb_storage_media_removing_cb_t)(void);
void usb_storage_set_media_removing_callback(usb_storage_media_removing_cb_t callback);

// True between a successful mount and the drive being pulled. The UI polls
// this rather than being called back, matching how it polls the player
// snapshot.
bool usb_storage_is_mounted(void);

// Distinguishes "no drive" from "a drive that failed to mount or read", which
// the UI needs to say something useful rather than "insert a drive" to someone
// who already did.
file_browser_media_t usb_storage_media(void);
