#pragma once

#include "player_control_types.h"
#include "station_catalog.h"
#include "usb_player_state.h"

#ifdef __cplusplus
extern "C" {
#endif

player_playback_state_t player_playback_from_radio(internet_radio_state_t state);
player_playback_state_t player_playback_from_usb(usb_player_state_t state);
bool player_snapshot_equal(const player_snapshot_t *left,
                           const player_snapshot_t *right);
bool player_rssi_refresh_due(bool seen, uint32_t last_update_ms,
                             uint32_t now_ms);

#ifdef ESP_PLATFORM
#include "esp_err.h"
#include "usb_browser.h"

esp_err_t player_control_init(void);
bool player_control_post(const player_command_t *command);
void player_control_get_snapshot(player_snapshot_t *snapshot);
const station_catalog_entry_t *player_control_station_at(size_t index);
// Copies out a row of the USB listing; false when the row is gone, which
// happens when the drive is pulled while the browser screen is open.
bool player_control_usb_entry_at(size_t index, usb_browser_entry_t *entry);
// Changes every time the listing is replaced, so a screen can tell "the same
// directory changed" from "this is a different directory".
unsigned int player_control_usb_listing_revision(void);
#endif

#ifdef __cplusplus
}
#endif
