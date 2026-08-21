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

static void test_healthy_active_source_reselect_is_noop(void)
{
    player_snapshot_t state = {.capabilities = PLAYER_CAP_INTERNET_RADIO,
                               .active_source = AUDIO_SOURCE_INTERNET_RADIO,
                               .playback_state = PLAYER_PLAYBACK_PLAYING};
    player_command_t command = {.kind = PLAYER_COMMAND_SELECT_SOURCE,
                                .source = AUDIO_SOURCE_INTERNET_RADIO};
    assert(player_control_decide(&state, &command) == PLAYER_OPERATION_NONE);
}

static void test_failed_active_source_can_be_reselected(void)
{
    player_snapshot_t state = {.capabilities = PLAYER_CAP_INTERNET_RADIO,
                               .active_source = AUDIO_SOURCE_INTERNET_RADIO,
                               .playback_state = PLAYER_PLAYBACK_ERROR};
    player_command_t command = {.kind = PLAYER_COMMAND_SELECT_SOURCE,
                                .source = AUDIO_SOURCE_INTERNET_RADIO};
    assert(player_control_decide(&state, &command) == PLAYER_OPERATION_SELECT_SOURCE);
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
    right = left;
    right.sample_rate_hz = 44100;
    assert(!player_snapshot_equal(&left, &right));
}

static void test_rssi_refresh_is_limited_to_once_per_second(void)
{
    assert(player_rssi_refresh_due(false, 0, 5));
    assert(!player_rssi_refresh_due(true, 100, 1099));
    assert(player_rssi_refresh_due(true, 100, 1100));
    assert(!player_rssi_refresh_due(true, UINT32_MAX - 499, 499));
    assert(player_rssi_refresh_due(true, UINT32_MAX - 499, 500));
}

static void test_usb_source_requires_a_mounted_drive(void)
{
    player_snapshot_t state = {.capabilities = PLAYER_CAP_INTERNET_RADIO,
                               .active_source = AUDIO_SOURCE_NONE};
    player_command_t command = {.kind = PLAYER_COMMAND_SELECT_SOURCE,
                                .source = AUDIO_SOURCE_USB};
    assert(player_control_decide(&state, &command) == PLAYER_OPERATION_INVALID);

    state.capabilities |= PLAYER_CAP_USB;
    assert(player_control_decide(&state, &command) == PLAYER_OPERATION_SELECT_SOURCE);
}

static void test_usb_reselecting_the_same_entry_still_acts(void)
{
    /* On USB the entry under the cursor may be a directory, and re-entering the
     * directory you are already in has to work; only the radio may treat a
     * repeated selection as a no-op. */
    player_snapshot_t state = {.active_source = AUDIO_SOURCE_USB,
                               .playback_state = PLAYER_PLAYBACK_PLAYING,
                               .active_item_index = 2, .item_count = 7};
    player_command_t command = {.kind = PLAYER_COMMAND_SELECT_ITEM, .item_index = 2};
    assert(player_control_decide(&state, &command) == PLAYER_OPERATION_START_ITEM);
}

static void test_track_finished_advances_only_on_usb(void)
{
    player_command_t command = {.kind = PLAYER_COMMAND_TRACK_FINISHED};
    player_snapshot_t usb = {.active_source = AUDIO_SOURCE_USB,
                             .playback_state = PLAYER_PLAYBACK_STOPPED,
                             .item_count = 4};
    assert(player_control_decide(&usb, &command) == PLAYER_OPERATION_ADVANCE_ITEM);

    /* A radio stream that ends has failed; jumping to another station would
     * hide the failure. */
    player_snapshot_t radio = {.active_source = AUDIO_SOURCE_INTERNET_RADIO,
                               .playback_state = PLAYER_PLAYBACK_STOPPED,
                               .item_count = 4};
    assert(player_control_decide(&radio, &command) == PLAYER_OPERATION_NONE);

    player_snapshot_t idle = {.active_source = AUDIO_SOURCE_NONE};
    assert(player_control_decide(&idle, &command) == PLAYER_OPERATION_NONE);
}

static void test_reveal_needs_usb_with_something_playing(void)
{
    /* Opening the browser asks for the playing file's directory. Paused counts
     * as playing here - the track is still the one on screen. */
    player_snapshot_t usb = {.active_source = AUDIO_SOURCE_USB,
                             .playback_state = PLAYER_PLAYBACK_PLAYING};
    player_command_t command = {.kind = PLAYER_COMMAND_BROWSE_REVEAL,
                                .source = AUDIO_SOURCE_USB};
    assert(player_control_decide(&usb, &command) == PLAYER_OPERATION_BROWSE_REVEAL);
    usb.playback_state = PLAYER_PLAYBACK_PAUSED;
    assert(player_control_decide(&usb, &command) == PLAYER_OPERATION_BROWSE_REVEAL);

    /* Stopped, or failed: the listing stays where the user left it rather than
     * jumping back to a file the drive is no longer playing. */
    usb.playback_state = PLAYER_PLAYBACK_STOPPED;
    assert(player_control_decide(&usb, &command) == PLAYER_OPERATION_NONE);
    usb.playback_state = PLAYER_PLAYBACK_ERROR;
    assert(player_control_decide(&usb, &command) == PLAYER_OPERATION_NONE);

    /* The station list is flat and has no directory to reveal. */
    player_snapshot_t radio = {.active_source = AUDIO_SOURCE_INTERNET_RADIO,
                               .playback_state = PLAYER_PLAYBACK_PLAYING};
    assert(player_control_decide(&radio, &command) == PLAYER_OPERATION_INVALID);
}

static void test_usb_state_maps_to_public_playback_state(void)
{
    assert(player_playback_from_usb(USB_PLAYER_STATE_STOPPED) == PLAYER_PLAYBACK_STOPPED);
    assert(player_playback_from_usb(USB_PLAYER_STATE_STARTING) == PLAYER_PLAYBACK_CONNECTING);
    assert(player_playback_from_usb(USB_PLAYER_STATE_PLAYING) == PLAYER_PLAYBACK_PLAYING);
    assert(player_playback_from_usb(USB_PLAYER_STATE_PAUSED) == PLAYER_PLAYBACK_PAUSED);
    assert(player_playback_from_usb(USB_PLAYER_STATE_ERROR) == PLAYER_PLAYBACK_ERROR);
}

int main(void)
{
    test_toggle_maps_playing_to_pause();
    test_usb_source_requires_a_mounted_drive();
    test_usb_reselecting_the_same_entry_still_acts();
    test_track_finished_advances_only_on_usb();
    test_reveal_needs_usb_with_something_playing();
    test_usb_state_maps_to_public_playback_state();
    test_active_station_is_not_restarted();
    test_failed_active_station_can_be_retried();
    test_healthy_active_source_reselect_is_noop();
    test_failed_active_source_can_be_reselected();
    test_unavailable_source_is_rejected();
    test_radio_state_maps_to_public_playback_state();
    test_snapshot_equality_detects_bitrate_change();
    test_rssi_refresh_is_limited_to_once_per_second();
    puts("player_control_logic tests passed");
    return 0;
}
