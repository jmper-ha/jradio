#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "usb_browser.h"

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
usb_browser_media_t usb_storage_media(void);

// Reads `path` into the single shared listing. The listing is refilled in
// place, so a read invalidates whatever the previous one held.
esp_err_t usb_storage_read_directory(const char *path);

// Accessors copy out under the listing lock: the MSC task can unmount the
// drive and clear the listing while the UI task is walking it.
size_t usb_storage_entry_count(void);
bool usb_storage_entry_at(size_t index, usb_browser_entry_t *out);

// Index of `name` in the current listing, or the entry count when it is absent
// or unreadable. Used to find a remembered track after a restart.
size_t usb_storage_find_entry(const char *name);
bool usb_storage_current_path(char *out, size_t out_size);

// Index of the next playable file at or after `from`. Returns a value past the
// end of the listing when there is none left *and* when the listing could not
// be read, so a caller that stops on "past the end" stops in both cases. Never
// returns a valid index it is not sure about - answering 0 on failure would
// send track advance back to the first file. Used to advance to the next
// track.
size_t usb_storage_next_file(size_t from);
