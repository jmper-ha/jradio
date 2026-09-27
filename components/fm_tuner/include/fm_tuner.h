#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "rda5807.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The FM tuner on I2C0, as the wiring names it (board_has_fm_tuner()).
 *
 * init only finds the chip and says so in the log; it stays powered down
 * until something tunes it. Every call takes a lock: the player task tunes,
 * and every task that builds a snapshot reads the status. */
esp_err_t fm_tuner_init(void);
bool fm_tuner_present(void);

esp_err_t fm_tuner_power(bool on);
esp_err_t fm_tuner_tune(uint32_t khz);
// Starts a hardware seek; fm_tuner_status() says when it has stopped.
esp_err_t fm_tuner_seek(bool up);
esp_err_t fm_tuner_set_volume(uint8_t volume);
// The chip's own mute: the pause, since there is nothing to hold back.
esp_err_t fm_tuner_set_muted(bool muted);
esp_err_t fm_tuner_status(rda5807_status_t *status);

#ifdef __cplusplus
}
#endif
