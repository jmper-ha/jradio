#pragma once

/* The UART to the jradio-bt module: frames out, frames in, and a snapshot
 * of what the module last said. Device-only; the frame parsing behind it is
 * bt_link_model.c, which the host tests cover.
 *
 * A build without the module (BOARD_HAS_BLUETOOTH 0) compiles all of this
 * to stubs: init does nothing, alive is false, every send fails. Callers
 * need no #if of their own. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bt_link_model.h"
#include "esp_err.h"

esp_err_t bt_link_init(void);

/* Whether the module has answered within the last few seconds. */
bool bt_link_alive(void);

void bt_link_snapshot(bt_link_state_t *out);

/* The small part of the snapshot, for the callers that run on every
 * frame the web or the panel builds: the whole bt_link_state_t is half a
 * kilobyte of strings, and three copies of it on the httpd task's stack
 * were what took the S3 down when a phone's slider moved fast. */
typedef struct {
    jbt_status_t status;
    uint32_t position_ms;
    uint32_t duration_ms;
    uint32_t track_revision;
} bt_link_brief_t;

void bt_link_brief(bt_link_brief_t *out);

/* One field of the track at a time, copied out under the lock. */
void bt_link_track_text(char *title, size_t title_size, char *artist, size_t artist_size,
                        char *album, size_t album_size);
void bt_link_peer_name(char *out, size_t out_size);

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
