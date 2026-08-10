#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "web_view_model.h"

static void test_splits_ascii_and_utf8_separators(void)
{
    char artist[64];
    char title[128];
    web_view_split_title("  Sam Feldt - Be My Lover  ", artist, sizeof(artist),
                         title, sizeof(title));
    assert(strcmp(artist, "Sam Feldt") == 0);
    assert(strcmp(title, "Be My Lover") == 0);

    web_view_split_title("Artist " "\xE2\x80\x93" " Song - Live", artist,
                         sizeof(artist),
                         title, sizeof(title));
    assert(strcmp(artist, "Artist") == 0);
    assert(strcmp(title, "Song - Live") == 0);
}

static void test_preserves_unsplit_and_configured_station_names(void)
{
    char artist[64];
    char title[128];
    web_view_split_title("Радио Шоколад", artist, sizeof(artist),
                         title, sizeof(title));
    assert(strcmp(artist, "") == 0);
    assert(strcmp(title, "Радио Шоколад") == 0);

    web_view_split_player_title("Jazz - Lounge", "Jazz - Lounge", artist,
                                sizeof(artist), title, sizeof(title));
    assert(strcmp(artist, "") == 0);
    assert(strcmp(title, "Jazz - Lounge") == 0);
}

static void test_split_outputs_are_always_bounded(void)
{
    char artist[5];
    char title[5];
    web_view_split_title("Long Artist - Long Title", artist, sizeof(artist),
                         title, sizeof(title));
    assert(strcmp(artist, "Long") == 0);
    assert(strcmp(title, "Long") == 0);

    web_view_split_title(NULL, artist, sizeof(artist), title, sizeof(title));
    assert(strcmp(artist, "") == 0);
    assert(strcmp(title, "") == 0);
    web_view_split_title("ignored", NULL, 0U, NULL, 0U);
}

static void test_invalid_utf8_is_replaced_without_partial_output(void)
{
    static const char replacement[] = "\xEF\xBF\xBD";
    static const char surrogate[] = "A" "\xED\xA0\x80" "B";
    static const char above_unicode[] = "A" "\xF4\x90\x80\x80" "B";
    static const char overlong[] = "A" "\xE0\x80\x80" "B";
    char artist[16];
    char title[32];
    char expected[16];

    snprintf(expected, sizeof(expected), "A%sB", replacement);
    web_view_split_title(surrogate, artist, sizeof(artist), title, sizeof(title));
    assert(strcmp(artist, "") == 0);
    assert(strcmp(title, expected) == 0);
    web_view_split_title(above_unicode, artist, sizeof(artist), title, sizeof(title));
    assert(strcmp(title, expected) == 0);
    web_view_split_title(overlong, artist, sizeof(artist), title, sizeof(title));
    assert(strcmp(title, expected) == 0);

    char truncated[3];
    web_view_split_title("A" "\xE2\x82\xAC", artist, sizeof(artist),
                         truncated, sizeof(truncated));
    assert(strcmp(truncated, "A") == 0);
}

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
    test_splits_ascii_and_utf8_separators();
    test_preserves_unsplit_and_configured_station_names();
    test_split_outputs_are_always_bounded();
    test_invalid_utf8_is_replaced_without_partial_output();
    test_enum_tokens_and_labels_are_stable();
    test_snapshot_changes_are_grouped_by_section();
    puts("web_view_model tests passed");
    return 0;
}
