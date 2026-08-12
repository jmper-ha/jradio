#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "usb_browser.h"
#include "usb_player_state.h"

#define USB_PLAYER_TRACK_MAX_LEN 128
#define USB_PLAYER_CODEC_MAX_LEN 8

typedef struct {
    usb_player_state_t state;
    // File name only, not the full path: this is what the player screen shows
    // where the radio shows a station name.
    char track[USB_PLAYER_TRACK_MAX_LEN];
    char codec[USB_PLAYER_CODEC_MAX_LEN];
    uint32_t sample_rate_hz;
    uint16_t bitrate_kbps;
} usb_player_status_t;

// Called from the playback task once a track ends *by itself* - not after a
// stop and not after a failure. It runs on the playback task as it exits, so
// it must not call back into usb_player_play(): post the intent somewhere and
// return.
typedef void (*usb_player_finished_cb_t)(void);
void usb_player_set_finished_callback(usb_player_finished_cb_t callback);

esp_err_t usb_player_init(void);

// Starts `path`, replacing whatever was playing. Blocks until the previous
// track's task has released the I2S output, so the caller must not be the task
// that owns a short poll deadline.
esp_err_t usb_player_play(const char *path, const char *display_name,
                          usb_browser_format_t format);
esp_err_t usb_player_stop(void);
esp_err_t usb_player_pause(void);
esp_err_t usb_player_resume(void);
void usb_player_get_status(usb_player_status_t *status);
