#include <assert.h>
#include <stdio.h>

#include "player_control.h"

static void test_toggle_maps_playing_to_pause(void)
{
    player_snapshot_t state = {.active_source = AUDIO_SOURCE_INTERNET_RADIO,
                               .playback_state = PLAYER_PLAYBACK_PLAYING};
    player_command_t command = {.kind = PLAYER_COMMAND_TOGGLE};
    assert(player_control_decide(&state, &command) == PLAYER_OPERATION_PAUSE);
}

static void test_active_station_is_not_restarted(void)
{
    player_snapshot_t state = {.active_source = AUDIO_SOURCE_INTERNET_RADIO,
                               .playback_state = PLAYER_PLAYBACK_PLAYING,
                               .active_item_index = 2, .item_count = 7};
    player_command_t command = {.kind = PLAYER_COMMAND_SELECT_ITEM, .item_index = 2};
    assert(player_control_decide(&state, &command) == PLAYER_OPERATION_NONE);
}

static void test_failed_active_station_can_be_retried(void)
{
    player_snapshot_t state = {.active_source = AUDIO_SOURCE_INTERNET_RADIO,
                               .playback_state = PLAYER_PLAYBACK_ERROR,
                               .active_item_index = 2, .item_count = 7};
    player_command_t command = {.kind = PLAYER_COMMAND_SELECT_ITEM, .item_index = 2};
    assert(player_control_decide(&state, &command) == PLAYER_OPERATION_START_ITEM);
}

static void test_unavailable_source_is_rejected(void)
{
    player_snapshot_t state = {.capabilities = PLAYER_CAP_INTERNET_RADIO};
    player_command_t command = {.kind = PLAYER_COMMAND_SELECT_SOURCE,
                                .source = AUDIO_SOURCE_BLUETOOTH};
    assert(player_control_decide(&state, &command) == PLAYER_OPERATION_INVALID);
}

static void test_radio_state_maps_to_public_playback_state(void)
{
    assert(player_playback_from_radio(INTERNET_RADIO_STATE_CONNECTING) ==
           PLAYER_PLAYBACK_CONNECTING);
    assert(player_playback_from_radio(INTERNET_RADIO_STATE_RECONNECTING) ==
           PLAYER_PLAYBACK_RECONNECTING);
}

static void test_snapshot_equality_detects_bitrate_change(void)
{
    player_snapshot_t left = {.active_source = AUDIO_SOURCE_INTERNET_RADIO,
                              .playback_state = PLAYER_PLAYBACK_PLAYING,
                              .active_item_index = 1};
    player_snapshot_t right = left;
    assert(player_snapshot_equal(&left, &right));
    right.bitrate_kbps = 128;
    assert(!player_snapshot_equal(&left, &right));
}

int main(void)
{
    test_toggle_maps_playing_to_pause();
    test_active_station_is_not_restarted();
    test_failed_active_station_can_be_retried();
    test_unavailable_source_is_rejected();
    test_radio_state_maps_to_public_playback_state();
    test_snapshot_equality_detects_bitrate_change();
    puts("player_control_logic tests passed");
    return 0;
}
