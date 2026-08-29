#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "web_view_model.h"

static void test_enum_tokens_and_labels_are_stable(void)
{
    assert(strcmp(web_view_playback_name(PLAYER_PLAYBACK_STOPPED), "stopped") == 0);
    assert(strcmp(web_view_playback_name(PLAYER_PLAYBACK_CONNECTING), "connecting") == 0);
    assert(strcmp(web_view_playback_name(PLAYER_PLAYBACK_PLAYING), "playing") == 0);
    assert(strcmp(web_view_playback_name(PLAYER_PLAYBACK_PAUSED), "paused") == 0);
    assert(strcmp(web_view_playback_name(PLAYER_PLAYBACK_RECONNECTING), "reconnecting") == 0);
    assert(strcmp(web_view_playback_name(PLAYER_PLAYBACK_ERROR), "error") == 0);
    assert(strcmp(web_view_playback_name((player_playback_state_t)99), "unknown") == 0);

    assert(strcmp(web_view_playback_label(PLAYER_PLAYBACK_PLAYING), "Воспроизведение") == 0);
    assert(strcmp(web_view_playback_label(PLAYER_PLAYBACK_PAUSED), "Пауза") == 0);
    assert(strcmp(web_view_source_name(AUDIO_SOURCE_INTERNET_RADIO), "internet_radio") == 0);
    assert(strcmp(web_view_source_label(AUDIO_SOURCE_INTERNET_RADIO), "Интернет-радио") == 0);
    assert(strcmp(web_view_source_name((audio_source_t)99), "unknown") == 0);
    assert(strcmp(web_view_source_label((audio_source_t)99), "Неизвестный режим") == 0);
}

static player_snapshot_t sample_snapshot(void)
{
    player_snapshot_t snapshot = {
        .capabilities = PLAYER_CAP_INTERNET_RADIO,
        .active_source = AUDIO_SOURCE_INTERNET_RADIO,
        .playback_state = PLAYER_PLAYBACK_PLAYING,
        .active_item_index = 1U,
        .item_count = 5U,
        .bitrate_kbps = 128U,
        .sample_rate_hz = 44100U,
        .wifi_rssi_valid = true,
        .wifi_rssi_dbm = -67,
    };
    strcpy(snapshot.context, "Радио Шоколад");
    strcpy(snapshot.stream_title, "Artist - Title");
    strcpy(snapshot.codec, "MP3");
    return snapshot;
}

static void test_snapshot_changes_are_grouped_by_section(void)
{
    player_snapshot_t left = sample_snapshot();
    player_snapshot_t right = left;
    assert(web_view_snapshot_changes(&left, &right) == WEB_VIEW_SECTION_NONE);

    right.capabilities = 0U;
    assert(web_view_snapshot_changes(&left, &right) == WEB_VIEW_SECTION_CAPABILITIES);
    right = left;
    right.bitrate_kbps = 192U;
    assert(web_view_snapshot_changes(&left, &right) == WEB_VIEW_SECTION_PLAYER);
    right = left;
    right.sample_rate_hz = 48000U;
    assert(web_view_snapshot_changes(&left, &right) == WEB_VIEW_SECTION_PLAYER);
    right = left;
    /* Nothing else moves when a track's tags land - they are read after it is
     * already playing - so without this the page would go on showing the file
     * name until the track ended. */
    right.track_tag_revision = left.track_tag_revision + 1U;
    assert(web_view_snapshot_changes(&left, &right) == WEB_VIEW_SECTION_PLAYER);
    right = left;
    right.active_item_index = 2U;
    assert(web_view_snapshot_changes(&left, &right) == WEB_VIEW_SECTION_LIST);
    right = left;
    right.active_source = AUDIO_SOURCE_NONE;
    assert(web_view_snapshot_changes(&left, &right) ==
           (WEB_VIEW_SECTION_PLAYER | WEB_VIEW_SECTION_LIST));
    assert(web_view_snapshot_changes(NULL, &right) == WEB_VIEW_SECTION_ALL);
    assert(web_view_snapshot_changes(NULL, NULL) == WEB_VIEW_SECTION_NONE);
}

int main(void)
{
    test_enum_tokens_and_labels_are_stable();
    test_snapshot_changes_are_grouped_by_section();
    puts("web_view_model tests passed");
    return 0;
}
