#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "web_server.h"
#include "web_socket.h"
#include "station_catalog.h"

static void test_parse_accepts_ssid_and_password(void)
{
    wifi_network_t network;
    assert(web_server_parse_wifi_request("{\"ssid\":\"home\",\"password\":\"pw\"}",
                                         &network) == WEB_SERVER_PARSE_OK);
    assert(strcmp(network.ssid, "home") == 0);
    assert(strcmp(network.password, "pw") == 0);
}

static void test_parse_rejects_missing_or_empty_fields(void)
{
    wifi_network_t network;
    assert(web_server_parse_wifi_request("{\"ssid\":\"\"}", &network) ==
           WEB_SERVER_PARSE_INVALID);
    assert(web_server_parse_wifi_request("{\"ssid\":\"home\",\"password\":1}", &network) ==
           WEB_SERVER_PARSE_INVALID);
    assert(web_server_parse_wifi_request("{\"ssid\":\"home\",\"password\":\"pw\",}",
                                         &network) == WEB_SERVER_PARSE_INVALID);
}

static player_snapshot_t sample_player(void)
{
    player_snapshot_t player = {
        .capabilities = PLAYER_CAP_INTERNET_RADIO,
        .active_source = AUDIO_SOURCE_INTERNET_RADIO,
        .playback_state = PLAYER_PLAYBACK_PLAYING,
        .active_item_index = 0U,
        .item_count = 2U,
        .bitrate_kbps = 128U,
        .wifi_rssi_valid = true,
        .wifi_rssi_dbm = -67,
    };
    strcpy(player.context, "Радио Шоколад");
    strcpy(player.stream_title, "Sam Feldt - Be My Lover");
    strcpy(player.codec, "MP3");
    return player;
}

static web_socket_wifi_state_t sample_wifi(void)
{
    web_socket_wifi_state_t wifi = {
        .mode = WIFI_PROVISIONING_STA_CONNECTED,
        .save_pending = true,
        .last_error = 202,
        .saved_count = 1U,
    };
    strcpy(wifi.active_ssid, "home");
    strcpy(wifi.ipv4, "192.168.1.183");
    strcpy(wifi.saved_ssids[0], "home");
    return wifi;
}

static web_socket_settings_state_t sample_settings(void)
{
    web_socket_settings_state_t settings = {
        .known = true,
        .view = {
            .language = DEVICE_LANGUAGE_RU,
            .home_screen = DEVICE_HOME_SCREEN_FEED,
            .scroll = DEVICE_SCROLL_BOUNCE,
            .volume = 62U,
            .brightness = 45U,
            .autoplay = true,
            .yandex_music = true,
            .flip_vertical = false,
            .flip_horizontal = true,
            .home_screen_available = true,
            .yandex_available = true,
        },
    };
    return settings;
}

static void test_yandex_action_accepts_only_the_three_it_implements(void)
{
    assert(web_server_parse_yandex_action("{\"action\":\"begin\"}") ==
           WEB_SERVER_YANDEX_ACTION_BEGIN);
    assert(web_server_parse_yandex_action("{ \"action\" : \"cancel\" }") ==
           WEB_SERVER_YANDEX_ACTION_CANCEL);
    assert(web_server_parse_yandex_action("{\"action\":\"forget\"}") ==
           WEB_SERVER_YANDEX_ACTION_FORGET);
    assert(web_server_parse_yandex_action("{\"action\":\"refresh\"}") ==
           WEB_SERVER_YANDEX_ACTION_REFRESH);
}

static void test_yandex_action_rejects_anything_else(void)
{
    /* "forget" wipes the stored credential, so a request that is merely close
     * to valid must not be guessed at. */
    assert(web_server_parse_yandex_action("{\"action\":\"Forget\"}") ==
           WEB_SERVER_YANDEX_ACTION_INVALID);
    assert(web_server_parse_yandex_action("{\"action\":\"\"}") ==
           WEB_SERVER_YANDEX_ACTION_INVALID);
    assert(web_server_parse_yandex_action("{\"action\":\"begin\",\"action\":\"forget\"}") ==
           WEB_SERVER_YANDEX_ACTION_INVALID);
    assert(web_server_parse_yandex_action("{\"action\":\"begin\",}") ==
           WEB_SERVER_YANDEX_ACTION_INVALID);
    assert(web_server_parse_yandex_action("{\"other\":\"begin\"}") ==
           WEB_SERVER_YANDEX_ACTION_INVALID);
    assert(web_server_parse_yandex_action("{\"action\":42}") ==
           WEB_SERVER_YANDEX_ACTION_INVALID);
    assert(web_server_parse_yandex_action("{}") == WEB_SERVER_YANDEX_ACTION_INVALID);
    assert(web_server_parse_yandex_action("{\"action\":\"begin\"} trailing") ==
           WEB_SERVER_YANDEX_ACTION_INVALID);
    assert(web_server_parse_yandex_action("not json") == WEB_SERVER_YANDEX_ACTION_INVALID);
    assert(web_server_parse_yandex_action(NULL) == WEB_SERVER_YANDEX_ACTION_INVALID);
    /* Longer than the field it is read into: truncating would turn an unknown
     * verb into a known one. */
    assert(web_server_parse_yandex_action("{\"action\":\"begin_and_then_some_more\"}") ==
           WEB_SERVER_YANDEX_ACTION_INVALID);
}

static void test_command_result_is_exact_and_escaped(void)
{
    char output[256];
    assert(web_server_command_result(output, sizeof(output), "42", true, "") > 0);
    assert(strcmp(output,
                  "{\"type\":\"command.result\",\"id\":\"42\",\"ok\":true}") == 0);

    assert(web_server_command_result(output, sizeof(output), "42", false,
                                     "Ошибка \"сети\"") > 0);
    assert(strcmp(output,
                  "{\"type\":\"command.result\",\"id\":\"42\",\"ok\":false,"
                  "\"error\":\"Ошибка \\\"сети\\\"\"}") == 0);
    assert(web_server_command_result(output, 24U, "42", false,
                                     "Недоступный режим") == 0);
    assert(output[0] == '\0');
}

static void test_snapshot_has_exact_public_sections_and_no_secrets(void)
{
    const player_snapshot_t player = sample_player();
    const web_socket_wifi_state_t wifi = sample_wifi();
    const web_socket_settings_state_t settings = sample_settings();
    char output[WEB_PROTOCOL_EVENT_MAX + 1U];
    const int length = web_socket_serialize_event(
        output, sizeof(output), WEB_SOCKET_EVENT_SNAPSHOT, 1U, &player, &wifi,
        &settings);
    assert(length > 0);
    assert((size_t)length == strlen(output));
    assert(strstr(output, "password") == NULL);
    assert(strstr(output, "url") == NULL);

    cJSON *root = cJSON_Parse(output);
    assert(cJSON_IsObject(root));
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(root, "type")->valuestring,
                  "snapshot") == 0);
    assert(cJSON_GetObjectItemCaseSensitive(root, "revision")->valueint == 1);

    cJSON *capabilities = cJSON_GetObjectItemCaseSensitive(root, "capabilities");
    assert(cJSON_GetArraySize(capabilities) == 1);
    cJSON *capability = cJSON_GetArrayItem(capabilities, 0);
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(capability, "id")->valuestring,
                  "internet_radio") == 0);

    cJSON *player_json = cJSON_GetObjectItemCaseSensitive(root, "player");
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(player_json, "artist")->valuestring,
                  "Sam Feldt") == 0);
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(player_json, "title")->valuestring,
                  "Be My Lover") == 0);
    assert(cJSON_GetObjectItemCaseSensitive(player_json, "wifi_rssi_dbm")->valueint == -67);

    /* A header, not the names: those come from GET /api/stations, keyed by the
     * revision here. Asserting the absence of "items" is the point - it is
     * what keeps the frame independent of how many stations there are. */
    cJSON *list = cJSON_GetObjectItemCaseSensitive(root, "list");
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(list, "kind")->valuestring,
                  "stations") == 0);
    assert(cJSON_GetObjectItemCaseSensitive(list, "active_index")->valueint == 0);
    assert(cJSON_GetObjectItemCaseSensitive(list, "count")->valueint == 2);
    assert(cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(list, "revision")));
    assert(cJSON_GetObjectItemCaseSensitive(list, "items") == NULL);

    cJSON *wifi_json = cJSON_GetObjectItemCaseSensitive(root, "wifi");
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(wifi_json, "mode")->valuestring,
                  "sta_connected") == 0);
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(wifi_json, "active_ssid")->valuestring,
                  "home") == 0);
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(wifi_json,
                                                        "save_pending")));
    assert(cJSON_GetObjectItemCaseSensitive(wifi_json, "last_error")->valueint ==
           202);

    /* The settings ride in the same document, so a page that opens while the
     * knob is somewhere unusual shows that rather than the defaults. */
    cJSON *settings_json = cJSON_GetObjectItemCaseSensitive(root, "settings");
    assert(cJSON_IsObject(settings_json));
    assert(cJSON_GetObjectItemCaseSensitive(settings_json, "volume")->valueint == 62);
    assert(cJSON_GetObjectItemCaseSensitive(settings_json, "brightness")->valueint == 45);
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(settings_json,
                                                   "home_screen")->valuestring,
                  "feed") == 0);
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(settings_json,
                                                        "flip_horizontal")));
    cJSON_Delete(root);

    /* Nothing published yet: the key is left out entirely rather than filled
     * with the defaults, which the page would have no way to tell from real
     * values. */
    const web_socket_settings_state_t unknown = {0};
    assert(web_socket_serialize_event(
               output, sizeof(output), WEB_SOCKET_EVENT_SNAPSHOT, 1U, &player,
               &wifi, &unknown) > 0);
    assert(strstr(output, "\"settings\"") == NULL);
    assert(web_socket_serialize_event(
               output, sizeof(output), WEB_SOCKET_EVENT_SETTINGS_UPDATE, 1U,
               &player, &wifi, &unknown) == 0);

    char too_small[64];
    assert(web_socket_serialize_event(
               too_small, sizeof(too_small), WEB_SOCKET_EVENT_SNAPSHOT, 1U,
               &player, &wifi, &settings) == 0);
    assert(too_small[0] == '\0');
}

static void test_player_update_omits_unavailable_rssi_and_repairs_utf8(void)
{
    player_snapshot_t player = sample_player();
    player.wifi_rssi_valid = false;
    strcpy(player.stream_title, "Artist - bad");
    player.stream_title[12] = (char)0xC0;
    player.stream_title[13] = (char)0xAF;
    player.stream_title[14] = '\0';
    const web_socket_wifi_state_t wifi = sample_wifi();
    const web_socket_settings_state_t settings = sample_settings();
    char output[1024];
    assert(web_socket_serialize_event(output, sizeof(output),
                                      WEB_SOCKET_EVENT_PLAYER_UPDATE, 7U,
                                      &player, &wifi, &settings) > 0);
    assert(strstr(output, "\"type\":\"player.update\"") != NULL);
    assert(strstr(output, "wifi_rssi_dbm") == NULL);
    assert(strstr(output, "\xEF\xBF\xBD") != NULL);
    cJSON *root = cJSON_Parse(output);
    assert(cJSON_IsObject(root));
    cJSON_Delete(root);
}

static void test_section_diff_and_update_types_are_bounded(void)
{
    player_snapshot_t previous_player = sample_player();
    player_snapshot_t current_player = previous_player;
    web_socket_wifi_state_t previous_wifi = sample_wifi();
    web_socket_wifi_state_t current_wifi = previous_wifi;
    web_socket_settings_state_t previous_settings = sample_settings();
    web_socket_settings_state_t current_settings = previous_settings;

    assert(web_socket_changed_sections(&previous_player, &previous_wifi,
                                       &previous_settings, &current_player,
                                       &current_wifi, &current_settings) ==
           WEB_SOCKET_SECTION_NONE);
    current_player.active_item_index = 1U;
    strcpy(current_wifi.active_ssid, "backup");
    assert(web_socket_changed_sections(&previous_player, &previous_wifi,
                                       &previous_settings, &current_player,
                                       &current_wifi, &current_settings) ==
           (WEB_SOCKET_SECTION_LIST | WEB_SOCKET_SECTION_WIFI));

    previous_player = current_player;
    previous_wifi = current_wifi;
    current_wifi.save_pending = !current_wifi.save_pending;
    assert(web_socket_changed_sections(&previous_player, &previous_wifi,
                                       &previous_settings, &current_player,
                                       &current_wifi, &current_settings) ==
           WEB_SOCKET_SECTION_WIFI);

    /* One step of the volume knob is the whole reason this section exists: it
     * is how a settings page left open follows what is done at the device. */
    previous_wifi = current_wifi;
    current_settings.view.volume = (uint8_t)(current_settings.view.volume + 1U);
    assert(web_socket_changed_sections(&previous_player, &previous_wifi,
                                       &previous_settings, &current_player,
                                       &current_wifi, &current_settings) ==
           WEB_SOCKET_SECTION_SETTINGS);

    /* And settings arriving for the first time count as a change, so a client
     * that connected before the UI published is told once it has. */
    previous_settings = current_settings;
    previous_settings.known = false;
    assert((web_socket_changed_sections(&previous_player, &previous_wifi,
                                        &previous_settings, &current_player,
                                        &current_wifi, &current_settings) &
            WEB_SOCKET_SECTION_SETTINGS) != 0U);
    previous_settings = current_settings;

    const web_socket_event_kind_t kinds[] = {
        WEB_SOCKET_EVENT_CAPABILITIES_UPDATE,
        WEB_SOCKET_EVENT_LIST_UPDATE,
        WEB_SOCKET_EVENT_WIFI_UPDATE,
        WEB_SOCKET_EVENT_SETTINGS_UPDATE,
    };
    const char *types[] = {
        "capabilities.update", "list.update", "wifi.update", "settings.update",
    };
    char output[WEB_PROTOCOL_EVENT_MAX + 1U];
    for (size_t index = 0U; index < sizeof(kinds) / sizeof(kinds[0]); ++index) {
        assert(web_socket_serialize_event(output, sizeof(output), kinds[index],
                                          (uint32_t)(8U + index),
                                          &current_player, &current_wifi,
                                          &current_settings) > 0);
        cJSON *root = cJSON_Parse(output);
        assert(cJSON_IsObject(root));
        assert(strcmp(cJSON_GetObjectItemCaseSensitive(root, "type")->valuestring,
                      types[index]) == 0);
        cJSON_Delete(root);
    }
}

static void fill_json_expensive_text(char *output, size_t capacity)
{
    assert(capacity > 1U);
    for (size_t index = 0U; index + 1U < capacity; ++index) {
        output[index] = (index & 1U) == 0U ? '"' : '\\';
    }
    output[capacity - 1U] = '\0';
}

static void test_the_frame_does_not_grow_with_the_catalogue(void)
{
    /* The frame used to carry every station name and fitted only while there
     * were 32 of them. The names moved to GET /api/stations, and this is the
     * property that replaced "32 of them fit": the snapshot costs the same
     * whether the catalogue holds one station or all of them.
     *
     * Everything else is still the worst case it always was - the fields that
     * do travel, filled with the characters JSON has to escape. */
    player_snapshot_t player = sample_player();
    player.item_count = STATION_CATALOG_MAX_ENTRIES;
    player.active_item_index = STATION_CATALOG_MAX_ENTRIES - 1U;
    fill_json_expensive_text(player.context, sizeof(player.context));
    fill_json_expensive_text(player.stream_title, sizeof(player.stream_title));
    fill_json_expensive_text(player.codec, sizeof(player.codec));
    fill_json_expensive_text(player.error, sizeof(player.error));

    web_socket_wifi_state_t wifi = sample_wifi();
    wifi.saved_count = WIFI_SETTINGS_MAX_NETWORKS;
    fill_json_expensive_text(wifi.active_ssid, sizeof(wifi.active_ssid));
    for (size_t index = 0U; index < WIFI_SETTINGS_MAX_NETWORKS; ++index) {
        fill_json_expensive_text(wifi.saved_ssids[index],
                                 sizeof(wifi.saved_ssids[index]));
    }

    const web_socket_settings_state_t settings = sample_settings();
    char output[WEB_PROTOCOL_EVENT_MAX + 1U];
    const int length = web_socket_serialize_event(
        output, sizeof(output), WEB_SOCKET_EVENT_SNAPSHOT, UINT32_MAX,
        &player, &wifi, &settings);
    assert(length > 0);
    assert(length <= (int)WEB_PROTOCOL_EVENT_MAX);
    /* Not merely "it fits": the buffer this bounds sits in internal SRAM, so
     * the margin is worth knowing about before it is gone. The settings
     * section costs 270 bytes of it. */
    assert((int)WEB_PROTOCOL_EVENT_MAX - length >= 256);

    cJSON *root = cJSON_Parse(output);
    assert(cJSON_IsObject(root));
    cJSON *list = cJSON_GetObjectItemCaseSensitive(root, "list");
    assert(cJSON_GetObjectItemCaseSensitive(list, "count")->valueint ==
           (int)STATION_CATALOG_MAX_ENTRIES);
    assert(cJSON_GetObjectItemCaseSensitive(list, "items") == NULL);
    cJSON_Delete(root);

    /* The same snapshot with one station. Whatever is left of the difference
     * is the digits of the count and the active index, and nothing else - if a
     * name ever finds its way back into this frame, this is what catches it. */
    player.item_count = 1U;
    player.active_item_index = 0U;
    const int shortest = web_socket_serialize_event(
        output, sizeof(output), WEB_SOCKET_EVENT_SNAPSHOT, UINT32_MAX,
        &player, &wifi, &settings);
    assert(shortest > 0);
    assert(length - shortest <= 4);
}

static void test_protocol_rejection_close_codes_are_bounded(void)
{
    assert(web_socket_rejection_close_code(true, WEB_SOCKET_FRAME_TEXT,
                                           WEB_PROTOCOL_FRAME_MAX) == 0U);
    assert(web_socket_rejection_close_code(false, WEB_SOCKET_FRAME_TEXT,
                                           10U) == 1002U);
    assert(web_socket_rejection_close_code(true, WEB_SOCKET_FRAME_BINARY,
                                           10U) == 1002U);
    assert(web_socket_rejection_close_code(true, WEB_SOCKET_FRAME_CONTINUATION,
                                           10U) == 1002U);
    assert(web_socket_rejection_close_code(true, WEB_SOCKET_FRAME_TEXT,
                                           WEB_PROTOCOL_FRAME_MAX + 1U) ==
           1009U);
    assert(web_socket_rejection_close_code(true, WEB_SOCKET_FRAME_PING,
                                           126U) == 1002U);
}

static void test_revision_advances_only_after_complete_serialization(void)
{
    assert(web_socket_committed_revision(41U, false) == 41U);
    assert(web_socket_committed_revision(41U, true) == 42U);
    assert(web_socket_committed_revision(UINT32_MAX, true) == 1U);
}

int main(void)
{
    test_parse_accepts_ssid_and_password();
    test_parse_rejects_missing_or_empty_fields();
    test_yandex_action_accepts_only_the_three_it_implements();
    test_yandex_action_rejects_anything_else();
    test_command_result_is_exact_and_escaped();
    test_snapshot_has_exact_public_sections_and_no_secrets();
    test_player_update_omits_unavailable_rssi_and_repairs_utf8();
    test_section_diff_and_update_types_are_bounded();
    test_the_frame_does_not_grow_with_the_catalogue();
    test_protocol_rejection_close_codes_are_bounded();
    test_revision_advances_only_after_complete_serialization();
    puts("web_server tests passed");
    return 0;
}
