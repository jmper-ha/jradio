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

int main(void)
{
    test_rejected_commands_preserve_current_view();
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
    puts("ui_player_state tests passed");
    return 0;
}
