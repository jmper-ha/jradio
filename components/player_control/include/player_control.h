#pragma once

#include "player_control_types.h"
#include "station_catalog.h"

#ifdef __cplusplus
extern "C" {
#endif

player_playback_state_t player_playback_from_radio(internet_radio_state_t state);
bool player_snapshot_equal(const player_snapshot_t *left,
                           const player_snapshot_t *right);

#ifdef ESP_PLATFORM
#include "esp_err.h"

esp_err_t player_control_init(void);
bool player_control_post(const player_command_t *command);
void player_control_get_snapshot(player_snapshot_t *snapshot);
const station_catalog_entry_t *player_control_station_at(size_t index);
#endif

#ifdef __cplusplus
}
#endif
