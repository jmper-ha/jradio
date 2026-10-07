#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Asking the release whether there is something new, and installing it.
 *
 * The radio reads ota.json from the latest GitHub release five minutes after
 * it starts and then once a day - only what it needs to know whether to say
 * anything, and nothing about itself goes with the request. The answer is
 * shown on the web page; nothing appears on the panel until somebody presses
 * "Update" there. The two choices the user has are kept in settings.csv:
 * whether to ask at all, and a version to stay quiet about. */

typedef enum {
    OTA_CHECK_IDLE = 0,
    OTA_CHECK_RUNNING,
    OTA_CHECK_DONE,
    OTA_CHECK_FAILED,
} ota_check_state_t;

typedef struct {
    ota_check_state_t state;
    bool enabled;
    bool installing;
    /* The latest release and whether it is worth saying: newer than this
     * firmware and not the one skipped. Empty before the first answer. */
    char latest[32];
    bool available;
    char skipped[32];
    /* Wall-clock seconds of the last answer, 0 before one or without time. */
    int64_t checked_at;
    const char *error;
} ota_check_status_t;

esp_err_t ota_check_start(void);
void ota_check_get(ota_check_status_t *status);
/* The latest release's change list in one language, cut to `size`. */
size_t ota_check_copy_notes(bool english, char *out, size_t size);

esp_err_t ota_check_now(void);
esp_err_t ota_check_set_enabled(bool enabled);
/* Stay quiet about the latest release; the next one is offered again. */
esp_err_t ota_check_skip(void);
/* Download the latest release's web files and firmware and install them. */
esp_err_t ota_check_install(void);

#ifdef __cplusplus
}
#endif
