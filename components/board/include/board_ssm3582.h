#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The SSM3582 amplifier on I2C0, as the wiring names it.
 *
 * Its own file because it is a device on the shared bus (board_i2c.h) rather
 * than part of the amp pin logic (board_amplifier.h): the bus has the real-
 * time clock and the FM tuner on it already, and this chip adds gain, mute
 * and power to what the MUTE pin of an analogue amplifier could be asked.
 *
 * init finds the chip and says so in the log; it leaves the amplifier muted
 * and powered down, so a boot is quiet whatever the pins were doing. Every
 * call takes a lock: the UI task turns the knob, and the player's start/stop
 * paths mute and unmute. */
esp_err_t board_ssm3582_init(void);
bool board_ssm3582_present(void);

/* Knob setting, 0..100, as the UI keeps it. Written to both channels over
 * I2C (the chip links them) on every turn of the encoder. 0 is exact
 * silence - the mute bits, not an attenuation code - and the volume register
 * goes to its quietest setting with it. */
esp_err_t board_ssm3582_set_volume(uint8_t percent);
uint8_t board_ssm3582_volume(void);

/* The pause and the sound sent to a Bluetooth speaker: what
 * board_audio_set_dac_muted() does to a PCM5102's XSMT. This is the gate -
 * the chip stays muted however the knob is turned while it is asserted, and
 * the knob's zero position mutes just the same; the two compose, so a pause
 * that ends does not open a channel the knob has turned down. */
esp_err_t board_ssm3582_set_muted(bool muted);

/* The master power-down, for the pause long enough to be worth the chip's
 * idle current falling to nothing. Powered back up, the chip comes back
 * muted - the caller unmutes when there is something to play. */
esp_err_t board_ssm3582_set_powered(bool powered);

/* Checks that the chip still holds what was written to it - DAC control and
 * both volumes - and writes back what does not, naming it in the log. True
 * when it had to. The same idea as fm_tuner_repair(): a spoiled write on a
 * shared bus leaves a chip playing at the wrong level, which is silent only
 * in the sense that nobody hears the fault. */
bool board_ssm3582_repair(void);

/* Reads the volume registers back as the chip holds them, for the repair and
 * for diagnostics. ESP_ERR_INVALID_STATE when the chip is not there. */
esp_err_t board_ssm3582_read_volume(uint8_t *left, uint8_t *right);

#ifdef __cplusplus
}
#endif