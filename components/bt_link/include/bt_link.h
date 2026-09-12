#pragma once

/* The UART to the jradio-bt module: frames out, frames in, and a snapshot
 * of what the module last said. Device-only; the frame parsing behind it is
 * bt_link_model.c, which the host tests cover.
 *
 * A build without the module (BOARD_HAS_BLUETOOTH 0) compiles all of this
 * to stubs: init does nothing, alive is false, every send fails. Callers
 * need no #if of their own. */

#include <stdbool.h>
#include <stdint.h>

#include "bt_link_model.h"
#include "esp_err.h"

esp_err_t bt_link_init(void);

/* Whether the module has answered within the last few seconds. */
bool bt_link_alive(void);

void bt_link_snapshot(bt_link_state_t *out);

/* SET_MODE with its handshake: returns once the module has acked with the
 * mode asked for, or fails after `timeout_ms`. The caller owns the bus
 * order around this - release it before asking for sink, take it back only
 * after asking for off has returned. */
esp_err_t bt_link_set_mode(jbt_mode_t mode, uint32_t timeout_ms);

esp_err_t bt_link_pairing(bool on);
esp_err_t bt_link_passthrough(jbt_key_t key);
/* 0..127 */
esp_err_t bt_link_set_volume(uint8_t volume);
esp_err_t bt_link_set_name(const char *name);
esp_err_t bt_link_disconnect(void);
