#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "file_browser.h"

// The card's own mount point, alongside the USB drive's /usb0. FATFS is built
// with two volumes, which these two exactly fill.
#define SD_STORAGE_ROOT_PATH "/sd0"

/* Brings up the card's SPI bus, then mounts the card once to see whether one is
 * there, lists its root into the log, and unmounts it again.
 *
 * Blocks: about 110 ms with a card, less without one. Must be called before
 * ui_init() and internet_radio_init(), so that the mount's allocations are made
 * and released before LVGL takes its share of internal SRAM - interleaved with
 * LVGL's they split the one large free block, and the radio's decoder task then
 * cannot find 8 KB of contiguous stack.
 *
 * Returns an error only if the SPI bus itself could not be created. An empty
 * slot is not a failure - it is the normal state of a device nobody put a card
 * in - and is reported through the log. */
esp_err_t sd_storage_init(void);

/* Mounts the card, or does nothing if it is already mounted.
 *
 * The card is not kept mounted, because a mount is not free: it holds some
 * 2.8 KB of internal SRAM, and that is measurably the difference between the
 * largest contiguous internal block being about 10 KB and being under the 8 KB
 * that system_health watches for - the block TLS needs for an HTTPS stream, and
 * the radio's decoder task for its stack. Nothing is lost by mounting late:
 * only one audio source plays at a time, so a card that is being read is a
 * radio that is not.
 *
 * It is also the only way to notice a card at all. The slot has no card-detect
 * line (SDC_HAS_CARD_DETECT), so a card inserted after boot announces itself
 * to nothing; trying the mount is the announcement.
 *
 * ESP_FAIL means a card answered but its filesystem would not mount, which is
 * a different thing to say on screen than a slot with nothing in it. */
esp_err_t sd_storage_mount(void);

/* Releases the mount and its internal SRAM.
 *
 * Everything reading the card must have stopped and closed its files first -
 * this frees the FATFS state that an in-flight fread() is standing on. Same
 * contract as pulling the USB drive, and for the same reason. */
esp_err_t sd_storage_unmount(void);

bool sd_storage_is_mounted(void);

/* What the last mount attempt found, which is not the same question as
 * "mounted right now": the card is released as soon as its source is left, and
 * that is the firmware letting go, not the card leaving.
 *
 * READY therefore means "a card was there and readable the last time anyone
 * looked", which is what the menu needs to decide whether offering the source
 * is honest, and what the browser needs to say why it is empty. */
file_browser_media_t sd_storage_media(void);
