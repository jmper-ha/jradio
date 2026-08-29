#include <assert.h>
#include <stdio.h>

#include "ui_player_state.h"

static player_snapshot_t snapshot(audio_source_t source, size_t item_index,
                                  size_t item_count)
{
    player_snapshot_t value = {
        .active_source = source,
        .active_item_index = item_index,
        .item_count = item_count,
    };
    return value;
}

static player_command_t command(player_command_kind_t kind,
                                audio_source_t source, size_t item_index)
{
    player_command_t value = {
        .kind = kind,
        .source = source,
        .item_index = item_index,
    };
    return value;
}

static void test_rejected_commands_preserve_current_view(void)
{
    ui_player_state_t state;
    ui_player_state_init(&state);

    player_command_t select_source =
        command(PLAYER_COMMAND_SELECT_SOURCE, AUDIO_SOURCE_INTERNET_RADIO,
                PLAYER_ITEM_NONE);
    assert(!ui_player_state_apply_post_result(&state, &select_source, false, 10));
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_MENU);
    assert(!ui_player_state_is_pending(&state));

    player_snapshot_t radio =
        snapshot(AUDIO_SOURCE_INTERNET_RADIO, 1, 3);
    ui_player_state_apply_snapshot(&state, &radio, 20);
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_SOURCE);
    assert(ui_player_state_show_station_list(&state));

    player_command_t select_item =
        command(PLAYER_COMMAND_SELECT_ITEM, AUDIO_SOURCE_INTERNET_RADIO, 2);
    assert(!ui_player_state_apply_post_result(&state, &select_item, false, 30));
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_STATION_LIST);
    assert(!ui_player_state_is_pending(&state));

    player_command_t stop =
        command(PLAYER_COMMAND_STOP_SOURCE, AUDIO_SOURCE_NONE, PLAYER_ITEM_NONE);
    assert(!ui_player_state_apply_post_result(&state, &stop, false, 40));
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_STATION_LIST);
    assert(!ui_player_state_is_pending(&state));
}

static void test_first_snapshot_is_authoritative(void)
{
    ui_player_state_t state;
    ui_player_state_init(&state);

    player_snapshot_t radio =
        snapshot(AUDIO_SOURCE_INTERNET_RADIO, 2, 4);
    ui_player_state_apply_snapshot(&state, &radio, 10);

    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_SOURCE);
    assert(ui_player_state_source(&state) == AUDIO_SOURCE_INTERNET_RADIO);
    assert(ui_player_state_active_item(&state) == 2);
}

static void test_stale_snapshot_keeps_pending_command_then_timeout_restores(void)
{
    ui_player_state_t state;
    ui_player_state_init(&state);
    player_snapshot_t radio =
        snapshot(AUDIO_SOURCE_INTERNET_RADIO, 1, 3);
    ui_player_state_apply_snapshot(&state, &radio, 0);
    assert(ui_player_state_show_station_list(&state));

    player_command_t select_item =
        command(PLAYER_COMMAND_SELECT_ITEM, AUDIO_SOURCE_INTERNET_RADIO, 2);
    assert(ui_player_state_apply_post_result(&state, &select_item, true, 100));
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_SOURCE);
    assert(ui_player_state_is_pending(&state));

    ui_player_state_apply_snapshot(&state, &radio,
                                   100 + UI_PLAYER_PENDING_TIMEOUT_MS - 1);
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_SOURCE);
    assert(ui_player_state_is_pending(&state));

    ui_player_state_apply_snapshot(&state, &radio,
                                   100 + UI_PLAYER_PENDING_TIMEOUT_MS);
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_STATION_LIST);
    assert(!ui_player_state_is_pending(&state));
}

static void test_confirmed_snapshot_clears_pending_command(void)
{
    ui_player_state_t state;
    ui_player_state_init(&state);
    player_snapshot_t stopped = snapshot(AUDIO_SOURCE_NONE, PLAYER_ITEM_NONE, 3);
    ui_player_state_apply_snapshot(&state, &stopped, 0);

    player_command_t select_source =
        command(PLAYER_COMMAND_SELECT_SOURCE, AUDIO_SOURCE_INTERNET_RADIO,
                PLAYER_ITEM_NONE);
    assert(ui_player_state_apply_post_result(&state, &select_source, true, 10));
    assert(ui_player_state_is_pending(&state));

    player_snapshot_t connecting =
        snapshot(AUDIO_SOURCE_INTERNET_RADIO, PLAYER_ITEM_NONE, 3);
    ui_player_state_apply_snapshot(&state, &connecting, 20);
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_SOURCE);
    assert(!ui_player_state_is_pending(&state));
}

static void test_retry_same_failed_item_requires_playback_progress(void)
{
    ui_player_state_t state;
    ui_player_state_init(&state);
    player_snapshot_t failed =
        snapshot(AUDIO_SOURCE_INTERNET_RADIO, 1, 3);
    failed.playback_state = PLAYER_PLAYBACK_ERROR;
    ui_player_state_apply_snapshot(&state, &failed, 0);
    assert(ui_player_state_show_station_list(&state));

    player_command_t retry =
        command(PLAYER_COMMAND_SELECT_ITEM, AUDIO_SOURCE_INTERNET_RADIO, 1);
    assert(ui_player_state_apply_post_result(&state, &retry, true, 100));
    ui_player_state_apply_snapshot(&state, &failed, 200);
    assert(ui_player_state_is_pending(&state));

    player_snapshot_t connecting = failed;
    connecting.playback_state = PLAYER_PLAYBACK_CONNECTING;
    ui_player_state_apply_snapshot(&state, &connecting, 300);
    assert(!ui_player_state_is_pending(&state));
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_SOURCE);
}

static void test_pending_wait_outlasts_decoder_stop_timeout(void)
{
    ui_player_state_t state;
    ui_player_state_init(&state);
    player_snapshot_t radio =
        snapshot(AUDIO_SOURCE_INTERNET_RADIO, 1, 3);
    radio.playback_state = PLAYER_PLAYBACK_PLAYING;
    ui_player_state_apply_snapshot(&state, &radio, 0);
    assert(ui_player_state_show_station_list(&state));

    player_command_t select_item =
        command(PLAYER_COMMAND_SELECT_ITEM, AUDIO_SOURCE_INTERNET_RADIO, 2);
    assert(ui_player_state_apply_post_result(&state, &select_item, true, 100));
    ui_player_state_apply_snapshot(&state, &radio, 12100);
    assert(ui_player_state_is_pending(&state));
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_SOURCE);
}

static void test_late_retry_confirmation_converges_to_source_view(void)
{
    ui_player_state_t state;
    ui_player_state_init(&state);
    player_snapshot_t failed =
        snapshot(AUDIO_SOURCE_INTERNET_RADIO, 1, 3);
    failed.playback_state = PLAYER_PLAYBACK_ERROR;
    ui_player_state_apply_snapshot(&state, &failed, 0);
    assert(ui_player_state_show_station_list(&state));

    player_command_t retry =
        command(PLAYER_COMMAND_SELECT_ITEM, AUDIO_SOURCE_INTERNET_RADIO, 1);
    assert(ui_player_state_apply_post_result(&state, &retry, true, 100));
    ui_player_state_apply_snapshot(&state, &failed,
                                   100 + UI_PLAYER_PENDING_TIMEOUT_MS);
    assert(!ui_player_state_is_pending(&state));
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_STATION_LIST);

    player_snapshot_t connecting = failed;
    connecting.playback_state = PLAYER_PLAYBACK_CONNECTING;
    ui_player_state_apply_snapshot(&state, &connecting,
                                   100 + UI_PLAYER_PENDING_TIMEOUT_MS + 1);
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_SOURCE);
}

static void test_state_changing_overlap_is_rejected_before_post(void)
{
    ui_player_state_t state;
    ui_player_state_init(&state);
    player_snapshot_t playing =
        snapshot(AUDIO_SOURCE_INTERNET_RADIO, 0, 3);
    playing.playback_state = PLAYER_PLAYBACK_PLAYING;
    ui_player_state_apply_snapshot(&state, &playing, 0);
    assert(ui_player_state_show_station_list(&state));

    player_command_t first =
        command(PLAYER_COMMAND_SELECT_ITEM, AUDIO_SOURCE_INTERNET_RADIO, 1);
    assert(ui_player_state_can_post(&state, &first));
    assert(ui_player_state_apply_post_result(&state, &first, true, 100));
    assert(ui_player_state_is_pending(&state));
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_SOURCE);

    const player_command_t overlapping[] = {
        command(PLAYER_COMMAND_SELECT_SOURCE, AUDIO_SOURCE_INTERNET_RADIO,
                PLAYER_ITEM_NONE),
        command(PLAYER_COMMAND_SELECT_ITEM, AUDIO_SOURCE_INTERNET_RADIO, 2),
    };
    for (size_t index = 0; index < sizeof(overlapping) / sizeof(overlapping[0]);
         ++index) {
        assert(!ui_player_state_can_post(&state, &overlapping[index]));
    }
    assert(ui_player_state_is_pending(&state));
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_SOURCE);

    player_command_t toggle =
        command(PLAYER_COMMAND_TOGGLE, AUDIO_SOURCE_NONE, PLAYER_ITEM_NONE);
    assert(ui_player_state_can_post(&state, &toggle));
    assert(ui_player_state_apply_post_result(&state, &toggle, true, 200));
    assert(ui_player_state_is_pending(&state));
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_SOURCE);
}

static void test_stop_supersedes_pending_station_selection(void)
{
    ui_player_state_t state;
    ui_player_state_init(&state);
    player_snapshot_t playing =
        snapshot(AUDIO_SOURCE_INTERNET_RADIO, 0, 3);
    playing.playback_state = PLAYER_PLAYBACK_PLAYING;
    ui_player_state_apply_snapshot(&state, &playing, 0);
    assert(ui_player_state_show_station_list(&state));

    player_command_t select_item =
        command(PLAYER_COMMAND_SELECT_ITEM, AUDIO_SOURCE_INTERNET_RADIO, 1);
    assert(ui_player_state_apply_post_result(&state, &select_item, true, 100));
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_SOURCE);

    player_command_t stop =
        command(PLAYER_COMMAND_STOP_SOURCE, AUDIO_SOURCE_NONE, PLAYER_ITEM_NONE);
    assert(ui_player_state_can_post(&state, &stop));
    assert(ui_player_state_apply_post_result(&state, &stop, true, 200));
    assert(ui_player_state_is_pending(&state));
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_MENU);

    ui_player_state_apply_snapshot(&state, &playing, 300);
    assert(ui_player_state_is_pending(&state));
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_MENU);

    player_snapshot_t selecting =
        snapshot(AUDIO_SOURCE_INTERNET_RADIO, 1, 3);
    selecting.playback_state = PLAYER_PLAYBACK_CONNECTING;
    ui_player_state_apply_snapshot(&state, &selecting, 400);
    assert(ui_player_state_is_pending(&state));
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_MENU);

    player_snapshot_t stopped = snapshot(AUDIO_SOURCE_NONE, PLAYER_ITEM_NONE, 3);
    ui_player_state_apply_snapshot(&state, &stopped, 500);
    assert(!ui_player_state_is_pending(&state));
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_MENU);
}

static void test_superseding_stop_waits_for_fifo_source_activation(void)
{
    ui_player_state_t state;
    ui_player_state_init(&state);
    player_snapshot_t stopped = snapshot(AUDIO_SOURCE_NONE, PLAYER_ITEM_NONE, 3);
    ui_player_state_apply_snapshot(&state, &stopped, 0);

    player_command_t select_source =
        command(PLAYER_COMMAND_SELECT_SOURCE, AUDIO_SOURCE_INTERNET_RADIO,
                PLAYER_ITEM_NONE);
    assert(ui_player_state_apply_post_result(&state, &select_source, true, 100));

    player_command_t stop =
        command(PLAYER_COMMAND_STOP_SOURCE, AUDIO_SOURCE_NONE, PLAYER_ITEM_NONE);
    assert(ui_player_state_can_post(&state, &stop));
    assert(ui_player_state_apply_post_result(&state, &stop, true, 200));
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_MENU);

    ui_player_state_apply_snapshot(&state, &stopped, 300);
    assert(ui_player_state_is_pending(&state));
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_MENU);

    player_snapshot_t connecting =
        snapshot(AUDIO_SOURCE_INTERNET_RADIO, PLAYER_ITEM_NONE, 3);
    connecting.playback_state = PLAYER_PLAYBACK_CONNECTING;
    ui_player_state_apply_snapshot(&state, &connecting, 400);
    assert(ui_player_state_is_pending(&state));
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_MENU);

    ui_player_state_apply_snapshot(&state, &connecting,
                                   200 + UI_PLAYER_PENDING_TIMEOUT_MS);
    assert(!ui_player_state_is_pending(&state));
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_SOURCE);

    ui_player_state_apply_snapshot(&state, &stopped,
                                   200 + UI_PLAYER_PENDING_TIMEOUT_MS + 1);
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_MENU);
}

static void test_queue_full_superseding_stop_preserves_first_pending(void)
{
    ui_player_state_t state;
    ui_player_state_init(&state);
    player_snapshot_t playing =
        snapshot(AUDIO_SOURCE_INTERNET_RADIO, 0, 3);
    playing.playback_state = PLAYER_PLAYBACK_PLAYING;
    ui_player_state_apply_snapshot(&state, &playing, 0);
    assert(ui_player_state_show_station_list(&state));

    player_command_t select_item =
        command(PLAYER_COMMAND_SELECT_ITEM, AUDIO_SOURCE_INTERNET_RADIO, 1);
    assert(ui_player_state_apply_post_result(&state, &select_item, true, 100));

    player_command_t stop =
        command(PLAYER_COMMAND_STOP_SOURCE, AUDIO_SOURCE_NONE, PLAYER_ITEM_NONE);
    assert(ui_player_state_can_post(&state, &stop));
    assert(!ui_player_state_apply_post_result(&state, &stop, false, 200));
    assert(ui_player_state_is_pending(&state));
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_SOURCE);

    player_snapshot_t selecting =
        snapshot(AUDIO_SOURCE_INTERNET_RADIO, 1, 3);
    selecting.playback_state = PLAYER_PLAYBACK_CONNECTING;
    ui_player_state_apply_snapshot(&state, &selecting, 300);
    assert(!ui_player_state_is_pending(&state));
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_SOURCE);
}

static void test_external_item_change_closes_station_list(void)
{
    ui_player_state_t state;
    ui_player_state_init(&state);
    player_snapshot_t first =
        snapshot(AUDIO_SOURCE_INTERNET_RADIO, 0, 3);
    ui_player_state_apply_snapshot(&state, &first, 0);
    assert(ui_player_state_show_station_list(&state));

    player_snapshot_t changed =
        snapshot(AUDIO_SOURCE_INTERNET_RADIO, 1, 3);
    ui_player_state_apply_snapshot(&state, &changed, 10);
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_SOURCE);
    assert(ui_player_state_active_item(&state) == 1);
}

static void test_close_station_list_returns_to_source(void)
{
    ui_player_state_t state;
    ui_player_state_init(&state);
    player_snapshot_t radio = snapshot(AUDIO_SOURCE_INTERNET_RADIO, 1, 3);
    ui_player_state_apply_snapshot(&state, &radio, 0);

    ui_player_state_close_station_list(&state);
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_SOURCE);

    assert(ui_player_state_show_station_list(&state));
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_STATION_LIST);
    ui_player_state_close_station_list(&state);
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_SOURCE);
}

static void test_close_station_list_redirects_pending_timeout_to_source(void)
{
    ui_player_state_t state;
    ui_player_state_init(&state);
    player_snapshot_t radio = snapshot(AUDIO_SOURCE_INTERNET_RADIO, 1, 3);
    ui_player_state_apply_snapshot(&state, &radio, 0);
    assert(ui_player_state_show_station_list(&state));

    player_command_t select_item =
        command(PLAYER_COMMAND_SELECT_ITEM, AUDIO_SOURCE_INTERNET_RADIO, 2);
    assert(ui_player_state_apply_post_result(&state, &select_item, true, 100));
    assert(ui_player_state_is_pending(&state));

    // Re-opening the station list while the switch is still pending (matches
    // ui_show_station_list being reachable again from the source screen).
    assert(ui_player_state_show_station_list(&state));
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_STATION_LIST);

    // Idle timeout fires: the list should close back to the source screen,
    // and a later pending-command timeout must not silently reopen it.
    ui_player_state_close_station_list(&state);
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_SOURCE);

    ui_player_state_apply_snapshot(&state, &radio,
                                   100 + UI_PLAYER_PENDING_TIMEOUT_MS);
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_SOURCE);
    assert(!ui_player_state_is_pending(&state));
}

static void test_list_view_opens_for_both_sources_with_a_list(void)
{
    ui_player_state_t state;

    ui_player_state_init(&state);
    player_snapshot_t usb = snapshot(AUDIO_SOURCE_USB, 0, 4);
    ui_player_state_apply_snapshot(&state, &usb, 10);
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_SOURCE);
    /* USB browses a directory in the same list view the radio uses. */
    assert(ui_player_state_show_station_list(&state));
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_STATION_LIST);

    /* Sources without a list of their own still have nothing to show. */
    ui_player_state_init(&state);
    player_snapshot_t bluetooth = snapshot(AUDIO_SOURCE_BLUETOOTH, 0, 0);
    ui_player_state_apply_snapshot(&state, &bluetooth, 10);
    assert(!ui_player_state_show_station_list(&state));
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_SOURCE);
}

static void test_usb_browsing_does_not_go_pending(void)
{
    ui_player_state_t state;
    ui_player_state_init(&state);
    player_snapshot_t usb = snapshot(AUDIO_SOURCE_USB, PLAYER_ITEM_NONE, 4);
    ui_player_state_apply_snapshot(&state, &usb, 10);
    assert(ui_player_state_show_station_list(&state));

    /* Opening a directory changes the listing, not the active item, so nothing
     * could ever confirm it; leaving it pending would block the screen until
     * the timeout. */
    player_command_t open = command(PLAYER_COMMAND_SELECT_ITEM, AUDIO_SOURCE_USB, 1);
    assert(ui_player_state_can_post(&state, &open));
    assert(ui_player_state_apply_post_result(&state, &open, true, 20));
    assert(!ui_player_state_is_pending(&state));
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_STATION_LIST);

    player_command_t up = command(PLAYER_COMMAND_BROWSE_UP, AUDIO_SOURCE_USB,
                                  PLAYER_ITEM_NONE);
    assert(ui_player_state_can_post(&state, &up));
    assert(ui_player_state_apply_post_result(&state, &up, true, 30));
    assert(!ui_player_state_is_pending(&state));

    /* Same for the reveal the browser posts as it opens: it moves the listing,
     * which no snapshot field the pending machinery watches would confirm. */
    player_command_t reveal = command(PLAYER_COMMAND_BROWSE_REVEAL, AUDIO_SOURCE_USB,
                                      PLAYER_ITEM_NONE);
    assert(ui_player_state_can_post(&state, &reveal));
    assert(ui_player_state_apply_post_result(&state, &reveal, true, 40));
    assert(!ui_player_state_is_pending(&state));
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_STATION_LIST);

    /* And for the seek the player screen posts: it changes where the file is
     * playing from, not what is playing, so nothing in a snapshot moves to
     * confirm it and it must not occupy the pending slot while the screen
     * waits for an answer that never comes. */
    player_command_t seek = command(PLAYER_COMMAND_SEEK, AUDIO_SOURCE_USB,
                                    PLAYER_ITEM_NONE);
    seek.position_seconds = 75U;
    assert(ui_player_state_can_post(&state, &seek));
    assert(ui_player_state_apply_post_result(&state, &seek, true, 50));
    assert(!ui_player_state_is_pending(&state));

    /* The radio keeps its pending window: a station switch blocks for seconds
     * while the decoder task stops. */
    ui_player_state_init(&state);
    player_snapshot_t radio = snapshot(AUDIO_SOURCE_INTERNET_RADIO, 0, 4);
    ui_player_state_apply_snapshot(&state, &radio, 10);
    player_command_t station =
        command(PLAYER_COMMAND_SELECT_ITEM, AUDIO_SOURCE_INTERNET_RADIO, 2);
    assert(ui_player_state_apply_post_result(&state, &station, true, 20));
    assert(ui_player_state_is_pending(&state));

    /* Browsing up is meaningless for a flat station list. */
    player_command_t radio_up =
        command(PLAYER_COMMAND_BROWSE_UP, AUDIO_SOURCE_INTERNET_RADIO, PLAYER_ITEM_NONE);
    assert(!ui_player_state_can_post(&state, &radio_up));
}

static void test_the_card_gets_the_same_views_as_the_drive(void)
{
    /* Both file sources share the browser view and the browse commands. When
     * this file still named AUDIO_SOURCE_USB, entering the card left the view
     * on the player screen while the home screen stayed on the display, and
     * every press after that went to a handler for a screen that was not
     * showing - the device looked frozen. */
    ui_player_state_t state;
    ui_player_state_init(&state);
    state.source = AUDIO_SOURCE_SD;
    assert(ui_player_state_show_station_list(&state));
    assert(ui_player_state_view(&state) == UI_PLAYER_VIEW_STATION_LIST);

    state.source = AUDIO_SOURCE_BLUETOOTH;
    assert(!ui_player_state_show_station_list(&state));

    /* Browsing the card must stay out of the pending machinery for the same
     * reason as the drive: no snapshot field moves to confirm a directory
     * change, so it would sit pending until the timeout with every further
     * press refused meanwhile. */
    ui_player_state_init(&state);
    const player_command_t browse = {.kind = PLAYER_COMMAND_BROWSE_UP,
                                     .source = AUDIO_SOURCE_SD,
                                     .item_index = PLAYER_ITEM_NONE};
    assert(ui_player_state_apply_post_result(&state, &browse, true, 0U));
    assert(!state.pending);
    const player_command_t select = {.kind = PLAYER_COMMAND_SELECT_ITEM,
                                     .source = AUDIO_SOURCE_SD,
                                     .item_index = 3U};
    assert(ui_player_state_apply_post_result(&state, &select, true, 0U));
    assert(!state.pending);
}

/* After a reboot, and after the web has stopped the source, the snapshot says
   no source at all - and still describes the station catalogue, which is what
   the list on screen is showing. The press used to be refused here, silently:
   no command, no notice, no log line, so the encoder looked dead. */
static void test_a_station_row_can_be_selected_without_a_source(void)
{
    ui_player_state_t state;
    ui_player_state_init(&state);

    player_snapshot_t idle = snapshot(AUDIO_SOURCE_NONE, PLAYER_ITEM_NONE, 18U);
    idle.playback_state = PLAYER_PLAYBACK_STOPPED;
    ui_player_state_apply_snapshot(&state, &idle, 10);
    assert(ui_player_state_can_select_item(&state, 7U));
    /* The catalogue's end still holds: the count comes from the same list. */
    assert(!ui_player_state_can_select_item(&state, 18U));

    player_command_t select_item =
        command(PLAYER_COMMAND_SELECT_ITEM, AUDIO_SOURCE_INTERNET_RADIO, 7U);
    assert(ui_player_state_can_post(&state, &select_item));
    assert(ui_player_state_apply_post_result(&state, &select_item, true, 20));

    /* The player adopts the radio as it starts the row, and that snapshot is
       what confirms the command rather than leaving it to time out. */
    player_snapshot_t playing = snapshot(AUDIO_SOURCE_INTERNET_RADIO, 7U, 18U);
    playing.playback_state = PLAYER_PLAYBACK_CONNECTING;
    ui_player_state_apply_snapshot(&state, &playing, 30);
    assert(!ui_player_state_is_pending(&state));
    assert(ui_player_state_source(&state) == AUDIO_SOURCE_INTERNET_RADIO);
}

/* The Yandex screen lives outside this view machine, but it posts through it:
 * first the source, then the row. Both steps used to be pinned to the internet
 * radio, so the second was refused for ever and the screen answered every
 * press with "not ready yet". */
static void test_a_yandex_row_can_be_selected_and_confirmed(void)
{
    ui_player_state_t state;
    ui_player_state_init(&state);

    /* Nothing may be chosen before a snapshot says the source is up and how
     * long its list is - that wait is what the screen steps through. */
    assert(!ui_player_state_can_select_item(&state, 0U));

    player_command_t select_source =
        command(PLAYER_COMMAND_SELECT_SOURCE, AUDIO_SOURCE_YANDEX, PLAYER_ITEM_NONE);
    assert(ui_player_state_can_post(&state, &select_source));
    assert(ui_player_state_apply_post_result(&state, &select_source, true, 10));
    assert(ui_player_state_is_pending(&state));

    /* While it is pending the row is still refused, which is why the screen
     * has to wait a pass rather than post both commands together. */
    player_command_t select_item =
        command(PLAYER_COMMAND_SELECT_ITEM, AUDIO_SOURCE_YANDEX, 2U);
    assert(!ui_player_state_can_post(&state, &select_item));

    player_snapshot_t linked = snapshot(AUDIO_SOURCE_YANDEX, PLAYER_ITEM_NONE, 4U);
    ui_player_state_apply_snapshot(&state, &linked, 20);
    assert(!ui_player_state_is_pending(&state));
    assert(ui_player_state_can_select_item(&state, 2U));
    /* A row past the end of the account's list is still refused. */
    assert(!ui_player_state_can_select_item(&state, 4U));

    assert(ui_player_state_can_post(&state, &select_item));
    assert(ui_player_state_apply_post_result(&state, &select_item, true, 30));
    assert(ui_player_state_is_pending(&state));

    /* And the snapshot that reports the station playing clears it, instead of
     * leaving it to time out thirteen seconds later. */
    player_snapshot_t playing = snapshot(AUDIO_SOURCE_YANDEX, 2U, 4U);
    playing.playback_state = PLAYER_PLAYBACK_CONNECTING;
    ui_player_state_apply_snapshot(&state, &playing, 40);
    assert(!ui_player_state_is_pending(&state));
    assert(ui_player_state_source(&state) == AUDIO_SOURCE_YANDEX);
}

static void test_the_marks_reach_the_player_without_going_pending(void)
{
    /* This gate is the screen's own, and it names every command it will let
     * through. The dislike shipped without being named here, so the key on the
     * front of the device did nothing while the identical command from the
     * browser - which never passes this way - worked. Nothing about the screen
     * looked wrong; the command was simply dropped one layer above the queue.
     *
     * Both marks are asserted, not only the one that broke: the like had no
     * test here either, which is why the omission was invisible. */
    ui_player_state_t state;
    ui_player_state_init(&state);

    player_snapshot_t playing = snapshot(AUDIO_SOURCE_YANDEX, PLAYER_ITEM_NONE, 4U);
    playing.playback_state = PLAYER_PLAYBACK_PLAYING;
    ui_player_state_apply_snapshot(&state, &playing, 10);

    const player_command_kind_t marks[] = {
        PLAYER_COMMAND_TOGGLE_LIKE,
        PLAYER_COMMAND_TOGGLE_DISLIKE,
        PLAYER_COMMAND_NEXT_TRACK,
    };
    for (size_t index = 0U; index < sizeof(marks) / sizeof(marks[0]); ++index) {
        player_command_t mark = command(marks[index], AUDIO_SOURCE_YANDEX, PLAYER_ITEM_NONE);
        assert(ui_player_state_can_post(&state, &mark));
        assert(ui_player_state_apply_post_result(&state, &mark, true, 20));
        /* And none of them waits on a snapshot: a mark is not playback, so
         * nothing in the snapshot would ever confirm it and the screen would
         * sit pending until the timeout - refusing every press in between. */
        assert(!ui_player_state_is_pending(&state));
    }

    /* A second press right after the first is accepted too, which is what
     * makes the double press on the device's like key possible at all. */
    player_command_t again =
        command(PLAYER_COMMAND_TOGGLE_DISLIKE, AUDIO_SOURCE_YANDEX, PLAYER_ITEM_NONE);
    assert(ui_player_state_can_post(&state, &again));
}

int main(void)
{
    test_the_card_gets_the_same_views_as_the_drive();
    test_a_yandex_row_can_be_selected_and_confirmed();
    test_a_station_row_can_be_selected_without_a_source();
    test_the_marks_reach_the_player_without_going_pending();
    test_rejected_commands_preserve_current_view();
    test_list_view_opens_for_both_sources_with_a_list();
    test_usb_browsing_does_not_go_pending();
    test_first_snapshot_is_authoritative();
    test_stale_snapshot_keeps_pending_command_then_timeout_restores();
    test_confirmed_snapshot_clears_pending_command();
    test_pending_wait_outlasts_decoder_stop_timeout();
    test_late_retry_confirmation_converges_to_source_view();
    test_state_changing_overlap_is_rejected_before_post();
    test_stop_supersedes_pending_station_selection();
    test_superseding_stop_waits_for_fifo_source_activation();
    test_queue_full_superseding_stop_preserves_first_pending();
    test_retry_same_failed_item_requires_playback_progress();
    test_external_item_change_closes_station_list();
    test_close_station_list_returns_to_source();
    test_close_station_list_redirects_pending_timeout_to_source();
    puts("ui_player_state tests passed");
    return 0;
}
