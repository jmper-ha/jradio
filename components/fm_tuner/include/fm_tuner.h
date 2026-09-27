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
/* The last RDS group, if a new one is in: `ready` false and nothing read
 * otherwise. The two flags are the chip's word on blocks A and B - it keeps
 * no count for C and D. */
/* Checks that the I2S output is still set up the way it was written, and
 * writes it again if not. True when it had to. For the FP on the bench,
 * whose I2C runs beside its own I2S clock: a spoiled write can land in
 * another register, and the sound stopped with the output switched off. */
bool fm_tuner_repair_i2s(void);
esp_err_t fm_tuner_read_rds(uint16_t blocks[4], bool *ready, bool *block_a_ok, bool *block_b_ok);

#ifdef __cplusplus
}
#endif
