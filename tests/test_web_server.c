#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "web_server.h"
#include "web_socket.h"

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

static const char *station_label(size_t index, void *context)
{
    const char *const *labels = context;
    return labels[index];
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

static void test_yandex_action_accepts_only_the_three_it_implements(void)
{
    assert(web_server_parse_yandex_action("{\"action\":\"begin\"}") ==
           WEB_SERVER_YANDEX_ACTION_BEGIN);
    assert(web_server_parse_yandex_action("{ \"action\" : \"cancel\" }") ==
           WEB_SERVER_YANDEX_ACTION_CANCEL);
    assert(web_server_parse_yandex_action("{\"action\":\"forget\"}") ==
           WEB_SERVER_YANDEX_ACTION_FORGET);
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
    const char *labels[] = {"Радио \"Шоколад\"", "Jazz Lounge"};
    const player_snapshot_t player = sample_player();
    const web_socket_wifi_state_t wifi = sample_wifi();
    char output[WEB_PROTOCOL_EVENT_MAX + 1U];
    const int length = web_socket_serialize_event(
        output, sizeof(output), WEB_SOCKET_EVENT_SNAPSHOT, 1U, &player, &wifi,
        station_label, (void *)labels);
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

    cJSON *list = cJSON_GetObjectItemCaseSensitive(root, "list");
    assert(cJSON_GetObjectItemCaseSensitive(list, "active_index")->valueint == 0);
    assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(list, "items")) == 2);

    cJSON *wifi_json = cJSON_GetObjectItemCaseSensitive(root, "wifi");
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(wifi_json, "mode")->valuestring,
                  "sta_connected") == 0);
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(wifi_json, "active_ssid")->valuestring,
                  "home") == 0);
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(wifi_json,
                                                        "save_pending")));
    assert(cJSON_GetObjectItemCaseSensitive(wifi_json, "last_error")->valueint ==
           202);
    cJSON_Delete(root);

    char too_small[64];
    assert(web_socket_serialize_event(
               too_small, sizeof(too_small), WEB_SOCKET_EVENT_SNAPSHOT, 1U,
               &player, &wifi, station_label, (void *)labels) == 0);
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
    char output[1024];
    assert(web_socket_serialize_event(output, sizeof(output),
                                      WEB_SOCKET_EVENT_PLAYER_UPDATE, 7U,
                                      &player, &wifi, NULL, NULL) > 0);
    assert(strstr(output, "\"type\":\"player.update\"") != NULL);
    assert(strstr(output, "wifi_rssi_dbm") == NULL);
    assert(strstr(output, "\xEF\xBF\xBD") != NULL);
    cJSON *root = cJSON_Parse(output);
    assert(cJSON_IsObject(root));
    cJSON_Delete(root);
}

static void test_section_diff_and_update_types_are_bounded(void)
{
    const char *labels[] = {"Radio 1", "Radio 2"};
    player_snapshot_t previous_player = sample_player();
    player_snapshot_t current_player = previous_player;
    web_socket_wifi_state_t previous_wifi = sample_wifi();
    web_socket_wifi_state_t current_wifi = previous_wifi;

    assert(web_socket_changed_sections(&previous_player, &previous_wifi,
                                       &current_player, &current_wifi) ==
           WEB_SOCKET_SECTION_NONE);
    current_player.active_item_index = 1U;
    strcpy(current_wifi.active_ssid, "backup");
    assert(web_socket_changed_sections(&previous_player, &previous_wifi,
                                       &current_player, &current_wifi) ==
           (WEB_SOCKET_SECTION_LIST | WEB_SOCKET_SECTION_WIFI));

    previous_player = current_player;
    previous_wifi = current_wifi;
    current_wifi.save_pending = !current_wifi.save_pending;
    assert(web_socket_changed_sections(&previous_player, &previous_wifi,
                                       &current_player, &current_wifi) ==
           WEB_SOCKET_SECTION_WIFI);

    const web_socket_event_kind_t kinds[] = {
        WEB_SOCKET_EVENT_CAPABILITIES_UPDATE,
        WEB_SOCKET_EVENT_LIST_UPDATE,
        WEB_SOCKET_EVENT_WIFI_UPDATE,
    };
    const char *types[] = {
        "capabilities.update", "list.update", "wifi.update",
    };
    char output[WEB_PROTOCOL_EVENT_MAX + 1U];
    for (size_t index = 0U; index < sizeof(kinds) / sizeof(kinds[0]); ++index) {
        assert(web_socket_serialize_event(output, sizeof(output), kinds[index],
                                          (uint32_t)(8U + index),
                                          &current_player, &current_wifi,
                                          station_label, (void *)labels) > 0);
        cJSON *root = cJSON_Parse(output);
        assert(cJSON_IsObject(root));
        assert(strcmp(cJSON_GetObjectItemCaseSensitive(root, "type")->valuestring,
                      types[index]) == 0);
        cJSON_Delete(root);
    }
}

static const char *worst_case_station_label(size_t index, void *context)
{
    (void)index;
    return context;
}

static void fill_json_expensive_text(char *output, size_t capacity)
{
    assert(capacity > 1U);
    for (size_t index = 0U; index + 1U < capacity; ++index) {
        output[index] = (index & 1U) == 0U ? '"' : '\\';
    }
    output[capacity - 1U] = '\0';
}

static void test_worst_case_catalog_always_fits_complete_snapshot(void)
{
    char station_name[96];
    fill_json_expensive_text(station_name, sizeof(station_name));
    player_snapshot_t player = sample_player();
    player.item_count = 32U;
    player.active_item_index = 31U;
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

    char output[WEB_PROTOCOL_EVENT_MAX + 1U];
    const int length = web_socket_serialize_event(
        output, sizeof(output), WEB_SOCKET_EVENT_SNAPSHOT, UINT32_MAX,
        &player, &wifi, worst_case_station_label, station_name);
    assert(length > 0);
    assert(length <= (int)WEB_PROTOCOL_EVENT_MAX);
    cJSON *root = cJSON_Parse(output);
    assert(cJSON_IsObject(root));
    cJSON *items = cJSON_GetObjectItemCaseSensitive(
        cJSON_GetObjectItemCaseSensitive(root, "list"), "items");
    assert(cJSON_GetArraySize(items) == 32);
    for (size_t index = 0U; index < 32U; ++index) {
        cJSON *label = cJSON_GetObjectItemCaseSensitive(
            cJSON_GetArrayItem(items, (int)index), "label");
        assert(cJSON_IsString(label));
    }
    cJSON_Delete(root);
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
    test_worst_case_catalog_always_fits_complete_snapshot();
    test_protocol_rejection_close_codes_are_bounded();
    test_revision_advances_only_after_complete_serialization();
    puts("web_server tests passed");
    return 0;
}
