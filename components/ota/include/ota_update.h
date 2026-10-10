#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "ota_image.h"
#include "ota_tar.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Writing a new firmware into the app slot that is not running, and
 * switching to it.
 *
 * An update goes to whichever app slot the device did not boot from: ota_0 or
 * ota_1 on the layout of v1.6.1 and later, factory or ota_0 on a board whose
 * table is older (see partitions.csv). Nothing the device boots changes until
 * ota_update_confirm(): a file written and then declined only leaves bytes in
 * a slot nobody starts.
 *
 * The writer is called from the HTTP worker for an upload. The confirm and
 * cancel come from the panel: the LAN web has no password, so a firmware sent
 * from a browser is installed only after somebody presses the encoder. */

typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_RECEIVING,
    OTA_STATE_CONFIRM,
    OTA_STATE_RESTARTING,
    OTA_STATE_FAILED,
} ota_state_t;

typedef struct {
    ota_state_t state;
    size_t done;
    size_t total;
    char version[32];   /* the one being written or waiting to be confirmed */
    const char *error;  /* a code word for the page, or NULL */
    uint32_t serial;    /* changes on every state change, for the panel */
} ota_status_t;

void ota_update_get_status(ota_status_t *status);

/* Checks the head (at least OTA_IMAGE_HEAD_SIZE bytes, fewer only if the
 * whole file is shorter), stops the sound, opens the slot and writes the head.
 * total is the whole file's size. *why says what was wrong when the file
 * itself is refused. */
esp_err_t ota_update_begin(const uint8_t *head, size_t head_length, size_t total,
                           ota_image_result_t *why);
esp_err_t ota_update_write(const void *data, size_t length);
/* Verifies what was written; with `ask`, the panel then asks for the press.
 * Without, ota_update_install() is the caller's next step. */
esp_err_t ota_update_finish(bool ask);
/* The upload broke off: whatever was written is left for the next one. */
void ota_update_abort(const char *error);

/* The web files, www.tar, unpacked beside the live pages. Sent before the
 * firmware when both are installed; app_follows says so, and the question
 * then waits for the firmware instead of being asked twice. Alone, the
 * archive is asked about on its own. */
esp_err_t ota_update_www_begin(size_t total);
ota_tar_result_t ota_update_www_write(const void *data, size_t length);
ota_tar_result_t ota_update_www_finish(bool app_follows);
void ota_update_www_abort(void);

/* Early at boot, once LittleFS is mounted: finishes or undoes a swap of the
 * web files that a restart or a power cut interrupted. */
void ota_update_boot_cleanup(void);

/* From the panel. confirm switches the boot slot and restarts a moment later,
 * so the page has time to read that it is restarting. */
esp_err_t ota_update_confirm(void);
void ota_update_cancel(void);
/* The update from the release, which nobody is asked about: the user chose it
 * on the page, and it can only ever be the official build. */
esp_err_t ota_update_install(void);

/* The label of the slot this firmware runs from - "ota_0", "ota_1" or, on an
 * older table, "factory" - for the page. */
const char *ota_update_running_slot(void);

/* The bootloader's rollback: a firmware started from an OTA slot for the
 * first time is on probation, and if the board resets before it says it is
 * well, the bootloader starts the one before it again. This says so thirty
 * seconds after it is called - long enough for a firmware that crashes at
 * start to have crashed. Call once from app_main, after everything is up. */
void ota_update_confirm_after_boot(void);

#ifdef __cplusplus
}
#endif
