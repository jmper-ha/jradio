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
/* The device the module is sending to, while it is: address as text and
 * the name it gave. False - and both empty - otherwise. */
bool bt_link_output_peer(char *address, size_t address_size, char *name, size_t name_size);
/* The module's firmware as "1.0.0", for the About page; empty while the
 * module has not answered. */
void bt_link_module_version(char *out, size_t out_size);

/* SET_MODE with its handshake: returns once the module has acked with the
 * mode asked for, or fails after `timeout_ms`. The caller owns the bus
 * order around this - release it before asking for sink, take it back only
 * after asking for off has returned. */
esp_err_t bt_link_set_mode(jbt_mode_t mode, uint32_t timeout_ms);

/* The module as an output. While on, and whenever the player is not using
 * the module as a phone's sink, the link keeps the module in source mode
 * listening to the I2S bus and connected to `address`; off, the module is
 * left in off mode. player_control owns the sink; this owns the source,
 * and the two never ask for the module at once because the player's sink
 * source and the output are exclusive by construction (a phone playing
 * through the DAC and the DAC going to a speaker cannot both be). */
esp_err_t bt_link_set_output(bool enabled, const char *address);
/* The speaker is called three times after it is chosen or the output is
 * switched on, then left to call itself; this starts the three again - the
 * page's choice of the speaker, even the same one, is that request. */
void bt_link_output_call_again(void);
/* True while the player has the module as a sink - the phone's - and so
 * neither a scan nor the speaker is possible until that source is left. */
bool bt_link_output_held_by_phone(void);
/* Whether the output is on and the speaker connected: what the panel shows
 * beside the volume. */
bool bt_link_output_connected(void);

/* Starts (or stops) a scan for speakers; results accumulate in the scan
 * list, read with bt_link_scan_snapshot(). The module must be in source
 * mode, which set_output arranges. */
esp_err_t bt_link_scan(bool on);
void bt_link_scan_snapshot(bt_link_scan_t *out);
bool bt_link_scanning(void);

/* What the host is clocking on the bus, for the module's resampler. */
esp_err_t bt_link_i2s_format(uint32_t sample_rate, uint8_t bits, uint8_t channels);

/* A button pressed on the speaker while it is the output, delivered on
 * the link's task: the player turns it into the command the panel's own
 * buttons would send. */
typedef void (*bt_link_key_listener_t)(jbt_key_t key);
void bt_link_set_key_listener(bt_link_key_listener_t listener);

esp_err_t bt_link_pairing(bool on);
esp_err_t bt_link_passthrough(jbt_key_t key);
/* 0..127 */
esp_err_t bt_link_set_volume(uint8_t volume);
/* What the module calls itself to a phone or a speaker. Sent when it
 * differs from the last one, and again in every greeting; until it is set
 * the greeting uses the device's own name (device_settings_device_name). */
esp_err_t bt_link_set_name(const char *name);
esp_err_t bt_link_disconnect(void);
