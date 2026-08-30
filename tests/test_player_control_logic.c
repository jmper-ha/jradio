#include <assert.h>
#include <stdio.h>

#include "player_control.h"

/* Every fixture below is a device that is on a network: the two sources that
   stream from the internet are refused without one, which is its own test at
   the end rather than something every other case has to think about. */

static void test_toggle_maps_playing_to_pause(void)
{
    player_snapshot_t state = {.wifi_connected = true, .active_source = AUDIO_SOURCE_INTERNET_RADIO,
                               .playback_state = PLAYER_PLAYBACK_PLAYING};
    player_command_t command = {.kind = PLAYER_COMMAND_TOGGLE};
    assert(player_control_decide(&state, &command) == PLAYER_OPERATION_PAUSE);
}

static void test_active_station_is_not_restarted(void)
{
    player_snapshot_t state = {.wifi_connected = true, .active_source = AUDIO_SOURCE_INTERNET_RADIO,
                               .playback_state = PLAYER_PLAYBACK_PLAYING,
                               .active_item_index = 2, .item_count = 7};
    player_command_t command = {.kind = PLAYER_COMMAND_SELECT_ITEM, .item_index = 2};
    assert(player_control_decide(&state, &command) == PLAYER_OPERATION_NONE);
}

/* After a reboot no source is selected, yet both screens already show the
   station list: a press on a row must start the station rather than vanish. */
static void test_station_starts_without_a_selected_source(void)
{
    player_snapshot_t state = {.wifi_connected = true, .active_source = AUDIO_SOURCE_NONE,
                               .playback_state = PLAYER_PLAYBACK_STOPPED,
                               .active_item_index = PLAYER_ITEM_NONE,
                               .item_count = 7};
    player_command_t command = {.kind = PLAYER_COMMAND_SELECT_ITEM, .item_index = 2};
    assert(player_control_decide(&state, &command) == PLAYER_OPERATION_START_ITEM);
}

/* The catalogue's bounds still hold here: with no source the index comes from
   that same list, and there is no row past its end. */
static void test_station_index_past_the_catalog_is_refused_without_a_source(void)
{
    player_snapshot_t state = {.wifi_connected = true, .active_source = AUDIO_SOURCE_NONE,
                               .playback_state = PLAYER_PLAYBACK_STOPPED,
                               .item_count = 7};
    player_command_t command = {.kind = PLAYER_COMMAND_SELECT_ITEM, .item_index = 7};
    assert(player_control_decide(&state, &command) == PLAYER_OPERATION_INVALID);
}

/* Trying a station from the playlist editor interrupts whatever plays: one
   output cannot do both, and the button says what it will do. */
static void test_a_stream_test_is_always_accepted(void)
{
    player_snapshot_t idle = {.wifi_connected = true, .active_source = AUDIO_SOURCE_NONE,
                              .playback_state = PLAYER_PLAYBACK_STOPPED};
    player_snapshot_t busy = {.wifi_connected = true, .active_source = AUDIO_SOURCE_USB,
                              .playback_state = PLAYER_PLAYBACK_PLAYING};
    player_command_t command = {.kind = PLAYER_COMMAND_TEST_STREAM,
                                .source = AUDIO_SOURCE_INTERNET_RADIO,
                                .item_index = PLAYER_ITEM_NONE};
    assert(player_control_decide(&idle, &command) == PLAYER_OPERATION_TEST_STREAM);
    assert(player_control_decide(&busy, &command) == PLAYER_OPERATION_TEST_STREAM);
}

static void test_failed_active_station_can_be_retried(void)
{
    player_snapshot_t state = {.wifi_connected = true, .active_source = AUDIO_SOURCE_INTERNET_RADIO,
                               .playback_state = PLAYER_PLAYBACK_ERROR,
                               .active_item_index = 2, .item_count = 7};
    player_command_t command = {.kind = PLAYER_COMMAND_SELECT_ITEM, .item_index = 2};
    assert(player_control_decide(&state, &command) == PLAYER_OPERATION_START_ITEM);
}

static void test_healthy_active_source_reselect_is_noop(void)
{
    player_snapshot_t state = {.wifi_connected = true, .capabilities = PLAYER_CAP_INTERNET_RADIO,
                               .active_source = AUDIO_SOURCE_INTERNET_RADIO,
                               .playback_state = PLAYER_PLAYBACK_PLAYING};
    player_command_t command = {.kind = PLAYER_COMMAND_SELECT_SOURCE,
                                .source = AUDIO_SOURCE_INTERNET_RADIO};
    assert(player_control_decide(&state, &command) == PLAYER_OPERATION_NONE);
}

static void test_failed_active_source_can_be_reselected(void)
{
    player_snapshot_t state = {.wifi_connected = true, .capabilities = PLAYER_CAP_INTERNET_RADIO,
                               .active_source = AUDIO_SOURCE_INTERNET_RADIO,
                               .playback_state = PLAYER_PLAYBACK_ERROR};
    player_command_t command = {.kind = PLAYER_COMMAND_SELECT_SOURCE,
                                .source = AUDIO_SOURCE_INTERNET_RADIO};
    assert(player_control_decide(&state, &command) == PLAYER_OPERATION_SELECT_SOURCE);
}

static void test_unavailable_source_is_rejected(void)
{
    player_snapshot_t state = {.wifi_connected = true, .capabilities = PLAYER_CAP_INTERNET_RADIO};
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
    player_snapshot_t left = {.wifi_connected = true, .active_source = AUDIO_SOURCE_INTERNET_RADIO,
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
    player_snapshot_t state = {.wifi_connected = true, .capabilities = PLAYER_CAP_INTERNET_RADIO,
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
    player_snapshot_t state = {.wifi_connected = true, .active_source = AUDIO_SOURCE_USB,
                               .playback_state = PLAYER_PLAYBACK_PLAYING,
                               .active_item_index = 2, .item_count = 7};
    player_command_t command = {.kind = PLAYER_COMMAND_SELECT_ITEM, .item_index = 2};
    assert(player_control_decide(&state, &command) == PLAYER_OPERATION_START_ITEM);
}

static void test_track_finished_advances_only_on_usb(void)
{
    player_command_t command = {.kind = PLAYER_COMMAND_TRACK_FINISHED};
    player_snapshot_t usb = {.wifi_connected = true, .active_source = AUDIO_SOURCE_USB,
                             .playback_state = PLAYER_PLAYBACK_STOPPED,
                             .item_count = 4};
    assert(player_control_decide(&usb, &command) == PLAYER_OPERATION_ADVANCE_ITEM);

    /* A radio stream that ends has failed; jumping to another station would
     * hide the failure. */
    player_snapshot_t radio = {.wifi_connected = true, .active_source = AUDIO_SOURCE_INTERNET_RADIO,
                               .playback_state = PLAYER_PLAYBACK_STOPPED,
                               .item_count = 4};
    assert(player_control_decide(&radio, &command) == PLAYER_OPERATION_NONE);

    player_snapshot_t idle = {.wifi_connected = true, .active_source = AUDIO_SOURCE_NONE};
    assert(player_control_decide(&idle, &command) == PLAYER_OPERATION_NONE);
}

static void test_reveal_needs_files_with_something_playing(void)
{
    /* Opening the browser asks for the playing file's directory. Paused counts
     * as playing here - the track is still the one on screen. */
    player_snapshot_t usb = {.wifi_connected = true, .active_source = AUDIO_SOURCE_USB,
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
    player_snapshot_t radio = {.wifi_connected = true, .active_source = AUDIO_SOURCE_INTERNET_RADIO,
                               .playback_state = PLAYER_PLAYBACK_PLAYING};
    assert(player_control_decide(&radio, &command) == PLAYER_OPERATION_INVALID);
}

static void test_seek_belongs_to_a_file_that_is_running(void)
{
    /* Playing is the state a seek normally arrives in - the file keeps
     * playing while the encoder scrubs it. Paused is accepted too: the file is
     * still open at a position, and the jump waits for the resume. */
    player_snapshot_t usb = {.wifi_connected = true, .active_source = AUDIO_SOURCE_USB,
                             .playback_state = PLAYER_PLAYBACK_PAUSED};
    player_command_t command = {.kind = PLAYER_COMMAND_SEEK,
                                .source = AUDIO_SOURCE_USB,
                                .position_seconds = 90U};
    assert(player_control_decide(&usb, &command) == PLAYER_OPERATION_SEEK);
    usb.playback_state = PLAYER_PLAYBACK_PLAYING;
    assert(player_control_decide(&usb, &command) == PLAYER_OPERATION_SEEK);

    /* Nothing is decoding, so there is no position to move. Refused rather
     * than treated as a no-op: a seek that silently did nothing would leave
     * the screen showing a place the file never went to. */
    usb.playback_state = PLAYER_PLAYBACK_STOPPED;
    assert(player_control_decide(&usb, &command) == PLAYER_OPERATION_INVALID);
    usb.playback_state = PLAYER_PLAYBACK_ERROR;
    assert(player_control_decide(&usb, &command) == PLAYER_OPERATION_INVALID);

    /* A stream has no length and no position - seeking one is meaningless
     * whatever it is doing. */
    player_snapshot_t radio = {.wifi_connected = true, .active_source = AUDIO_SOURCE_INTERNET_RADIO,
                               .playback_state = PLAYER_PLAYBACK_PLAYING};
    assert(player_control_decide(&radio, &command) == PLAYER_OPERATION_INVALID);
}

static void test_choosing_the_playing_file_again_does_not_restart_it(void)
{
    /* How the user gets back to the player from the browser: press the row the
     * track is on. Starting it over there loses the position they were at. */
    const char *playing = "/usb0/Album/03 track.mp3";
    assert(player_file_same_file_playing(playing, playing, PLAYER_PLAYBACK_PLAYING));
    assert(player_file_same_file_playing(playing, playing, PLAYER_PLAYBACK_PAUSED));
    /* Still opening counts too - a second press must not restart what has not
     * finished starting. */
    assert(player_file_same_file_playing(playing, playing, PLAYER_PLAYBACK_CONNECTING));

    /* A different file in the same directory is a different file. */
    assert(!player_file_same_file_playing("/usb0/Album/04 track.mp3", playing,
                                         PLAYER_PLAYBACK_PLAYING));
    /* And so is the same name in another directory. */
    assert(!player_file_same_file_playing("/usb0/Other/03 track.mp3", playing,
                                         PLAYER_PLAYBACK_PLAYING));

    /* A failed track has to stay restartable: the path outlives the decoder
     * that gave up on it. */
    assert(!player_file_same_file_playing(playing, playing, PLAYER_PLAYBACK_ERROR));
    assert(!player_file_same_file_playing(playing, playing, PLAYER_PLAYBACK_STOPPED));

    /* Nothing has played yet, so nothing can be the file that is playing. */
    assert(!player_file_same_file_playing(playing, "", PLAYER_PLAYBACK_PLAYING));
    assert(!player_file_same_file_playing(NULL, playing, PLAYER_PLAYBACK_PLAYING));
    assert(!player_file_same_file_playing(playing, NULL, PLAYER_PLAYBACK_PLAYING));
}

static void test_usb_state_maps_to_public_playback_state(void)
{
    assert(player_playback_from_file(FILE_PLAYER_STATE_STOPPED) == PLAYER_PLAYBACK_STOPPED);
    assert(player_playback_from_file(FILE_PLAYER_STATE_STARTING) == PLAYER_PLAYBACK_CONNECTING);
    assert(player_playback_from_file(FILE_PLAYER_STATE_PLAYING) == PLAYER_PLAYBACK_PLAYING);
    assert(player_playback_from_file(FILE_PLAYER_STATE_PAUSED) == PLAYER_PLAYBACK_PAUSED);
    assert(player_playback_from_file(FILE_PLAYER_STATE_ERROR) == PLAYER_PLAYBACK_ERROR);
}

static void test_each_volume_answers_for_itself(void)
{
    /* A mounted drive says nothing about the card slot, and one capability for
     * both would have let the card be selected whenever a stick happened to be
     * plugged in - and refused it whenever one was not. */
    player_snapshot_t state = {.wifi_connected = true, .capabilities = PLAYER_CAP_INTERNET_RADIO | PLAYER_CAP_USB,
                               .active_source = AUDIO_SOURCE_NONE};
    player_command_t card = {.kind = PLAYER_COMMAND_SELECT_SOURCE,
                             .source = AUDIO_SOURCE_SD};
    assert(player_control_decide(&state, &card) == PLAYER_OPERATION_INVALID);

    state.capabilities |= PLAYER_CAP_SD;
    assert(player_control_decide(&state, &card) == PLAYER_OPERATION_SELECT_SOURCE);

    // And the other way round: the card slot does not stand in for a drive.
    player_snapshot_t no_drive = {.wifi_connected = true, .capabilities = PLAYER_CAP_SD,
                                  .active_source = AUDIO_SOURCE_NONE};
    player_command_t drive = {.kind = PLAYER_COMMAND_SELECT_SOURCE,
                              .source = AUDIO_SOURCE_USB};
    assert(player_control_decide(&no_drive, &drive) == PLAYER_OPERATION_INVALID);
}

static void test_every_file_operation_works_on_the_card_too(void)
{
    /* The browser, track advance and seeking are one implementation for both
     * volumes; these used to name AUDIO_SOURCE_USB and would have silently
     * refused everything on the card. */
    player_snapshot_t card = {.wifi_connected = true, .active_source = AUDIO_SOURCE_SD,
                              .playback_state = PLAYER_PLAYBACK_PLAYING,
                              .active_item_index = 1, .item_count = 5};
    player_command_t up = {.kind = PLAYER_COMMAND_BROWSE_UP};
    assert(player_control_decide(&card, &up) == PLAYER_OPERATION_BROWSE_UP);

    player_command_t reveal = {.kind = PLAYER_COMMAND_BROWSE_REVEAL};
    assert(player_control_decide(&card, &reveal) == PLAYER_OPERATION_BROWSE_REVEAL);

    player_command_t seek = {.kind = PLAYER_COMMAND_SEEK, .position_seconds = 30U};
    assert(player_control_decide(&card, &seek) == PLAYER_OPERATION_SEEK);

    player_snapshot_t ended = {.wifi_connected = true, .active_source = AUDIO_SOURCE_SD,
                               .playback_state = PLAYER_PLAYBACK_STOPPED,
                               .item_count = 5};
    player_command_t finished = {.kind = PLAYER_COMMAND_TRACK_FINISHED};
    assert(player_control_decide(&ended, &finished) == PLAYER_OPERATION_ADVANCE_ITEM);
}

static void test_yandex_needs_a_linked_account(void)
{
    /* The capability is set only while an account is linked, and without one
     * every request would fail identically - so the source is refused here
     * rather than opening a screen that can only show an error. */
    player_snapshot_t state = {.wifi_connected = true, .capabilities = PLAYER_CAP_INTERNET_RADIO | PLAYER_CAP_SD};
    player_command_t command = {.kind = PLAYER_COMMAND_SELECT_SOURCE,
                                .source = AUDIO_SOURCE_YANDEX};
    assert(player_control_decide(&state, &command) == PLAYER_OPERATION_INVALID);

    state.capabilities |= PLAYER_CAP_YANDEX;
    assert(player_control_decide(&state, &command) == PLAYER_OPERATION_SELECT_SOURCE);
}

static void test_a_yandex_station_starts_and_is_not_restarted(void)
{
    player_snapshot_t state = {.wifi_connected = true, .capabilities = PLAYER_CAP_YANDEX,
                               .active_source = AUDIO_SOURCE_YANDEX,
                               .playback_state = PLAYER_PLAYBACK_STOPPED,
                               .active_item_index = PLAYER_ITEM_NONE,
                               .item_count = 4U};
    player_command_t command = {.kind = PLAYER_COMMAND_SELECT_ITEM, .item_index = 2U};
    assert(player_control_decide(&state, &command) == PLAYER_OPERATION_START_ITEM);

    /* Playing already: picking the same row again is the same no-op it is on
     * the radio, not a restart of the chain. */
    state.playback_state = PLAYER_PLAYBACK_PLAYING;
    state.active_item_index = 2U;
    assert(player_control_decide(&state, &command) == PLAYER_OPERATION_NONE);

    /* And a row that is not in the list is refused rather than clamped. */
    command.item_index = 4U;
    assert(player_control_decide(&state, &command) == PLAYER_OPERATION_INVALID);
}

static void test_a_yandex_chain_never_advances_by_itself(void)
{
    /* The end of a track is handled inside the audio path, which opens the
     * next link without telling anyone. If a TRACK_FINISHED ever did arrive
     * here it must not be read as "play the next station". */
    player_snapshot_t state = {.wifi_connected = true, .capabilities = PLAYER_CAP_YANDEX,
                               .active_source = AUDIO_SOURCE_YANDEX,
                               .playback_state = PLAYER_PLAYBACK_PLAYING};
    player_command_t command = {.kind = PLAYER_COMMAND_TRACK_FINISHED};
    assert(player_control_decide(&state, &command) == PLAYER_OPERATION_NONE);
}

static void test_the_track_keys_stop_at_the_ends_of_the_catalog(void)
{
    player_snapshot_t state = {.wifi_connected = true, .active_source = AUDIO_SOURCE_INTERNET_RADIO,
                               .playback_state = PLAYER_PLAYBACK_PLAYING,
                               .active_item_index = 1U,
                               .item_count = 3U};
    player_command_t next = {.kind = PLAYER_COMMAND_NEXT_ITEM};
    player_command_t previous = {.kind = PLAYER_COMMAND_PREVIOUS_ITEM};
    assert(player_control_decide(&state, &next) == PLAYER_OPERATION_NEXT_ITEM);
    assert(player_control_decide(&state, &previous) == PLAYER_OPERATION_PREVIOUS_ITEM);

    /* At the last station forward does nothing - and does *not* wrap round to
     * the first, which would hide the fact that the list has an end. */
    state.active_item_index = 2U;
    assert(player_control_decide(&state, &next) == PLAYER_OPERATION_NONE);
    assert(player_control_decide(&state, &previous) == PLAYER_OPERATION_PREVIOUS_ITEM);

    state.active_item_index = 0U;
    assert(player_control_decide(&state, &next) == PLAYER_OPERATION_NEXT_ITEM);
    assert(player_control_decide(&state, &previous) == PLAYER_OPERATION_NONE);

    /* Nothing playing: the keys move what is on the air, they do not choose
     * something to put there. */
    state.active_item_index = PLAYER_ITEM_NONE;
    assert(player_control_decide(&state, &next) == PLAYER_OPERATION_NONE);
    assert(player_control_decide(&state, &previous) == PLAYER_OPERATION_NONE);
}

static void test_the_track_keys_leave_the_ends_of_a_directory_to_the_executor(void)
{
    /* A listing holds directories as well as files, and this function cannot
     * see which is which - so both directions come back as work to do, and
     * whether there is a file to move to is settled where the entries are. */
    player_snapshot_t state = {.wifi_connected = true, .active_source = AUDIO_SOURCE_USB,
                               .playback_state = PLAYER_PLAYBACK_PLAYING,
                               .active_item_index = 0U,
                               .item_count = 1U};
    player_command_t next = {.kind = PLAYER_COMMAND_NEXT_ITEM};
    player_command_t previous = {.kind = PLAYER_COMMAND_PREVIOUS_ITEM};
    assert(player_control_decide(&state, &next) == PLAYER_OPERATION_NEXT_ITEM);
    assert(player_control_decide(&state, &previous) == PLAYER_OPERATION_PREVIOUS_ITEM);

    state.active_source = AUDIO_SOURCE_SD;
    assert(player_control_decide(&state, &next) == PLAYER_OPERATION_NEXT_ITEM);
}

static void test_the_rotor_has_no_track_keys_of_this_kind(void)
{
    /* It is a station source, but its chain has no previous track and no index
     * in any list: on that source the same two buttons mean the next track and
     * the like mark instead. */
    player_snapshot_t state = {.wifi_connected = true, .active_source = AUDIO_SOURCE_YANDEX,
                               .playback_state = PLAYER_PLAYBACK_PLAYING,
                               .active_item_index = 1U,
                               .item_count = 4U};
    player_command_t next = {.kind = PLAYER_COMMAND_NEXT_ITEM};
    player_command_t previous = {.kind = PLAYER_COMMAND_PREVIOUS_ITEM};
    assert(player_control_decide(&state, &next) == PLAYER_OPERATION_INVALID);
    assert(player_control_decide(&state, &previous) == PLAYER_OPERATION_INVALID);
}

static void test_the_like_mark_belongs_to_a_playing_rotor_track(void)
{
    player_snapshot_t state = {.wifi_connected = true, .active_source = AUDIO_SOURCE_YANDEX,
                               .playback_state = PLAYER_PLAYBACK_PLAYING};
    player_command_t command = {.kind = PLAYER_COMMAND_TOGGLE_LIKE};
    assert(player_control_decide(&state, &command) == PLAYER_OPERATION_TOGGLE_LIKE);

    /* Paused counts: the track is still the one being listened to. */
    state.playback_state = PLAYER_PLAYBACK_PAUSED;
    assert(player_control_decide(&state, &command) == PLAYER_OPERATION_TOGGLE_LIKE);

    /* Stopped there is no track to mark ... */
    state.playback_state = PLAYER_PLAYBACK_STOPPED;
    assert(player_control_decide(&state, &command) == PLAYER_OPERATION_INVALID);

    /* ... and a station or a file is in nobody's library. */
    state.active_source = AUDIO_SOURCE_INTERNET_RADIO;
    state.playback_state = PLAYER_PLAYBACK_PLAYING;
    assert(player_control_decide(&state, &command) == PLAYER_OPERATION_INVALID);
    state.active_source = AUDIO_SOURCE_USB;
    assert(player_control_decide(&state, &command) == PLAYER_OPERATION_INVALID);
}

static void test_a_stopped_file_source_can_be_started_again(void)
{
    /* What the play button means once a track has stopped - including the way
     * a directory ends, which is a stop like any other from here.
     *
     * The pure answer was always START_SAVED, for every source. The executor
     * was the half that disagreed: its START_SAVED branch restarted the radio
     * and the rotor and had `else if (!audio_source_is_files(...))` around it,
     * so on a file source the press was accepted and dropped. The end of a
     * directory then left a screen naming a track with every button doing
     * nothing, and the only way out was the list.
     *
     * This test cannot reach the executor - player_control.c is device-only -
     * so it pins the contract the executor has to honour, and the comment
     * above is the other half of the fix. */
    player_command_t toggle = {.kind = PLAYER_COMMAND_TOGGLE};
    const audio_source_t sources[] = {AUDIO_SOURCE_USB, AUDIO_SOURCE_SD};
    for (size_t index = 0U; index < sizeof(sources) / sizeof(sources[0]); ++index) {
        player_snapshot_t state = {.wifi_connected = true, .active_source = sources[index],
                                   .playback_state = PLAYER_PLAYBACK_STOPPED};
        assert(player_control_decide(&state, &toggle) == PLAYER_OPERATION_START_SAVED);
        /* A file that failed to decode is startable too - the same rule the
         * radio follows, and the reason a broken file is not a dead end. */
        state.playback_state = PLAYER_PLAYBACK_ERROR;
        assert(player_control_decide(&state, &toggle) == PLAYER_OPERATION_START_SAVED);
    }
}

static void test_the_track_keys_still_work_at_the_end_of_a_directory(void)
{
    /* The row the last track played from is kept rather than cleared, so the
     * keys have a place in the list to move from. Both are answered here; only
     * the executor knows whether a neighbouring *file* exists, since a listing
     * holds directories too. */
    player_snapshot_t state = {.wifi_connected = true, .active_source = AUDIO_SOURCE_USB,
                               .playback_state = PLAYER_PLAYBACK_STOPPED,
                               .active_item_index = 3U, .item_count = 4U};
    player_command_t previous = {.kind = PLAYER_COMMAND_PREVIOUS_ITEM};
    player_command_t next = {.kind = PLAYER_COMMAND_NEXT_ITEM};
    assert(player_control_decide(&state, &previous) == PLAYER_OPERATION_PREVIOUS_ITEM);
    assert(player_control_decide(&state, &next) == PLAYER_OPERATION_NEXT_ITEM);

    /* Cleared, they do nothing - which is what browsing away from the playing
     * directory leaves behind, and there is no row there to step from. */
    state.active_item_index = PLAYER_ITEM_NONE;
    assert(player_control_decide(&state, &previous) == PLAYER_OPERATION_NONE);
    assert(player_control_decide(&state, &next) == PLAYER_OPERATION_NONE);
}

static void test_the_rejection_answers_to_the_same_conditions(void)
{
    /* Its own command and its own operation, but the same question about
     * whether there is a track to mark at all - so the two are decided
     * together and only the answer differs. */
    player_snapshot_t state = {.wifi_connected = true, .active_source = AUDIO_SOURCE_YANDEX,
                               .playback_state = PLAYER_PLAYBACK_PLAYING};
    player_command_t command = {.kind = PLAYER_COMMAND_TOGGLE_DISLIKE};
    assert(player_control_decide(&state, &command) == PLAYER_OPERATION_TOGGLE_DISLIKE);

    state.playback_state = PLAYER_PLAYBACK_PAUSED;
    assert(player_control_decide(&state, &command) == PLAYER_OPERATION_TOGGLE_DISLIKE);

    /* The mark the track already carries changes nothing here: whether this
     * press sets the rejection or takes it off is the executor's to work out,
     * from the state at the moment the queue reaches it rather than the one
     * the button was drawn from. */
    state.playback_state = PLAYER_PLAYBACK_PLAYING;
    state.track_likeable = true;
    state.track_disliked = true;
    assert(player_control_decide(&state, &command) == PLAYER_OPERATION_TOGGLE_DISLIKE);

    state.playback_state = PLAYER_PLAYBACK_STOPPED;
    assert(player_control_decide(&state, &command) == PLAYER_OPERATION_INVALID);
    state.playback_state = PLAYER_PLAYBACK_PLAYING;
    state.active_source = AUDIO_SOURCE_INTERNET_RADIO;
    assert(player_control_decide(&state, &command) == PLAYER_OPERATION_INVALID);
    state.active_source = AUDIO_SOURCE_SD;
    assert(player_control_decide(&state, &command) == PLAYER_OPERATION_INVALID);
}

static void test_snapshot_equality_notices_the_like_mark(void)
{
    /* The web transport sends a section only when the snapshot differs, so a
     * mark that does not register here would never reach the browser. */
    player_snapshot_t left = {.wifi_connected = true, .active_source = AUDIO_SOURCE_YANDEX,
                              .track_likeable = true};
    player_snapshot_t right = left;
    assert(player_snapshot_equal(&left, &right));
    right.track_liked = true;
    assert(!player_snapshot_equal(&left, &right));

    /* And the other mark, which travels the same way and would otherwise
     * change on the device without the browser ever hearing about it. */
    right = left;
    right.track_disliked = true;
    assert(!player_snapshot_equal(&left, &right));
}

/* Without a network there is nothing to stream, and the only thing further
   down the road is a connection attempt that ends in "Connection error" on the
   screen. Refused here rather than only on the two faces, because a page
   loaded while the network was still up keeps its buttons live - a browser
   left open on the player is how this was found. */
static void test_streaming_sources_are_refused_without_a_network(void)
{
    player_command_t select = {.kind = PLAYER_COMMAND_SELECT_SOURCE,
                               .source = AUDIO_SOURCE_INTERNET_RADIO};
    player_snapshot_t offline = {.capabilities = PLAYER_CAP_INTERNET_RADIO |
                                                 PLAYER_CAP_YANDEX | PLAYER_CAP_USB,
                                 .active_source = AUDIO_SOURCE_NONE,
                                 .playback_state = PLAYER_PLAYBACK_STOPPED,
                                 .active_item_index = PLAYER_ITEM_NONE,
                                 .item_count = 7};
    assert(player_control_decide(&offline, &select) == PLAYER_OPERATION_INVALID);
    select.source = AUDIO_SOURCE_YANDEX;
    assert(player_control_decide(&offline, &select) == PLAYER_OPERATION_INVALID);
    /* The drive is not on the network and never was. */
    select.source = AUDIO_SOURCE_USB;
    assert(player_control_decide(&offline, &select) == PLAYER_OPERATION_SELECT_SOURCE);

    /* The row on the station list is a stream too, including on the list the
       snapshot describes before any source has been chosen. */
    const player_command_t station = {.kind = PLAYER_COMMAND_SELECT_ITEM, .item_index = 2};
    assert(player_control_decide(&offline, &station) == PLAYER_OPERATION_INVALID);

    /* And so is the play key, which would otherwise restart the last station
       from a screen that offers nothing else to press. */
    const player_command_t play = {.kind = PLAYER_COMMAND_PLAY};
    const player_command_t toggle = {.kind = PLAYER_COMMAND_TOGGLE};
    player_snapshot_t stopped_radio = {.active_source = AUDIO_SOURCE_INTERNET_RADIO,
                                       .playback_state = PLAYER_PLAYBACK_STOPPED};
    assert(player_control_decide(&stopped_radio, &play) == PLAYER_OPERATION_INVALID);
    assert(player_control_decide(&stopped_radio, &toggle) == PLAYER_OPERATION_INVALID);
    player_snapshot_t paused_rotor = {.active_source = AUDIO_SOURCE_YANDEX,
                                      .playback_state = PLAYER_PLAYBACK_PAUSED};
    assert(player_control_decide(&paused_rotor, &play) == PLAYER_OPERATION_INVALID);
    assert(player_control_decide(&paused_rotor, &toggle) == PLAYER_OPERATION_INVALID);

    /* Stopping is always allowed - that is the way out - and so is pausing
       something that is somehow still going. */
    player_snapshot_t playing_radio = {.active_source = AUDIO_SOURCE_INTERNET_RADIO,
                                       .playback_state = PLAYER_PLAYBACK_PLAYING};
    assert(player_control_decide(&playing_radio, &toggle) == PLAYER_OPERATION_PAUSE);
    const player_command_t stop = {.kind = PLAYER_COMMAND_STOP_SOURCE};
    assert(player_control_decide(&playing_radio, &stop) == PLAYER_OPERATION_STOP);

    /* A file on the drive is none of the network's business. */
    player_snapshot_t paused_files = {.active_source = AUDIO_SOURCE_USB,
                                      .playback_state = PLAYER_PLAYBACK_PAUSED};
    assert(player_control_decide(&paused_files, &play) == PLAYER_OPERATION_RESUME);
}

int main(void)
{
    test_the_track_keys_stop_at_the_ends_of_the_catalog();
    test_the_track_keys_leave_the_ends_of_a_directory_to_the_executor();
    test_the_rotor_has_no_track_keys_of_this_kind();
    test_the_like_mark_belongs_to_a_playing_rotor_track();
    test_a_stopped_file_source_can_be_started_again();
    test_the_track_keys_still_work_at_the_end_of_a_directory();
    test_the_rejection_answers_to_the_same_conditions();
    test_snapshot_equality_notices_the_like_mark();
    test_each_volume_answers_for_itself();
    test_every_file_operation_works_on_the_card_too();
    test_yandex_needs_a_linked_account();
    test_a_yandex_station_starts_and_is_not_restarted();
    test_a_yandex_chain_never_advances_by_itself();
    test_toggle_maps_playing_to_pause();
    test_usb_source_requires_a_mounted_drive();
    test_usb_reselecting_the_same_entry_still_acts();
    test_track_finished_advances_only_on_usb();
    test_reveal_needs_files_with_something_playing();
    test_seek_belongs_to_a_file_that_is_running();
    test_choosing_the_playing_file_again_does_not_restart_it();
    test_usb_state_maps_to_public_playback_state();
    test_active_station_is_not_restarted();
    test_failed_active_station_can_be_retried();
    test_station_starts_without_a_selected_source();
    test_station_index_past_the_catalog_is_refused_without_a_source();
    test_a_stream_test_is_always_accepted();
    test_healthy_active_source_reselect_is_noop();
    test_failed_active_source_can_be_reselected();
    test_unavailable_source_is_rejected();
    test_radio_state_maps_to_public_playback_state();
    test_snapshot_equality_detects_bitrate_change();
    test_rssi_refresh_is_limited_to_once_per_second();
    test_streaming_sources_are_refused_without_a_network();
    puts("player_control_logic tests passed");
    return 0;
}
