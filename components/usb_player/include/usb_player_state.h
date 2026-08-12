#pragma once

// Split out of usb_player.h so player_control_logic.c can map this enum in the
// host build, where esp_err.h does not exist - the same split internet_radio
// makes with internet_radio_state.h.

typedef enum {
    USB_PLAYER_STATE_STOPPED = 0,
    USB_PLAYER_STATE_STARTING,
    USB_PLAYER_STATE_PLAYING,
    USB_PLAYER_STATE_PAUSED,
    USB_PLAYER_STATE_ERROR,
} usb_player_state_t;
