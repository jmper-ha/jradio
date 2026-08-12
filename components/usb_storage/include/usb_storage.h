#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "usb_browser.h"

esp_err_t usb_storage_init(void);

// True between a successful mount and the drive being pulled. The UI polls
// this rather than being called back, matching how it polls the player
// snapshot.
bool usb_storage_is_mounted(void);

// Reads `path` into the single shared listing. The listing is refilled in
// place, so a read invalidates whatever the previous one held.
esp_err_t usb_storage_read_directory(const char *path);

// Accessors copy out under the listing lock: the MSC task can unmount the
// drive and clear the listing while the UI task is walking it.
size_t usb_storage_entry_count(void);
bool usb_storage_entry_at(size_t index, usb_browser_entry_t *out);
bool usb_storage_current_path(char *out, size_t out_size);

// Index of the next playable file at or after `from`, or the entry count when
// the listing has none left. Used to advance to the next track.
size_t usb_storage_next_file(size_t from);
