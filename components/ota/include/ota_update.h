#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "ota_image.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Writing a new firmware into the app slot that is not running, and
 * switching to it.
 *
 * The slots are factory and ota_0 and an update goes to whichever of the two
 * the device did not boot from - ESP-IDF's own next-slot helper never
 * answers factory, so the choice is made here. Nothing the device boots
 * changes until ota_update_confirm(): a file written and then declined only
 * leaves bytes in a slot nobody starts.
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
/* Verifies what was written and waits for the press. */
esp_err_t ota_update_finish(void);
/* The upload broke off: whatever was written is left for the next one. */
void ota_update_abort(const char *error);

/* From the panel. confirm switches the boot slot and restarts a moment later,
 * so the page has time to read that it is restarting. */
esp_err_t ota_update_confirm(void);
void ota_update_cancel(void);

/* "factory" or "ota_0": which slot this firmware runs from, for the page. */
const char *ota_update_running_slot(void);

#ifdef __cplusplus
}
#endif
