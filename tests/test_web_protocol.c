#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "web_protocol.h"

static size_t s_cjson_allocations;

static void *counting_malloc(size_t size)
{
    ++s_cjson_allocations;
    return malloc(size);
}

static void counting_free(void *memory)
{
    free(memory);
}

static web_protocol_result_t parse(const char *json, web_command_t *command)
{
    return web_protocol_parse_command(json, strlen(json), command);
}

static void assert_zeroed(const web_command_t *command)
{
    const unsigned char *bytes = (const unsigned char *)command;
    for (size_t index = 0; index < sizeof(*command); ++index) {
        assert(bytes[index] == 0U);
    }
}

static void assert_invalid_and_zeroed(const char *json)
{
    web_command_t command;
    memset(&command, 0xA5, sizeof(command));
    assert(parse(json, &command) == WEB_PROTOCOL_INVALID);
    assert_zeroed(&command);
}

static void test_accepts_exact_player_commands(void)
{
    web_command_t command;
    const char *toggle =
        "{\"type\":\"command\",\"id\":\"42\",\"action\":\"player.toggle\"}";
    assert(parse(toggle, &command) == WEB_PROTOCOL_OK);
    assert(command.kind == WEB_COMMAND_PLAYER);
    assert(command.player.kind == PLAYER_COMMAND_TOGGLE);
    assert(strcmp(command.request_id, "42") == 0);

    const char *play =
        "{\"action\":\"player.play\",\"id\":\"web-1.2\",\"type\":\"command\"}";
    assert(parse(play, &command) == WEB_PROTOCOL_OK);
    assert(command.player.kind == PLAYER_COMMAND_PLAY);

    const char *pause =
        "{\"id\":\"_pause\",\"type\":\"command\",\"action\":\"player.pause\"}";
    assert(parse(pause, &command) == WEB_PROTOCOL_OK);
    assert(command.player.kind == PLAYER_COMMAND_PAUSE);
}

static void test_accepts_source_and_station_selection(void)
{
    web_command_t command;
    const char *source =
        "{\"type\":\"command\",\"id\":\"source-1\","
        "\"action\":\"source.select\",\"source\":\"internet_radio\"}";
    assert(parse(source, &command) == WEB_PROTOCOL_OK);
    assert(command.kind == WEB_COMMAND_PLAYER);
    assert(command.player.kind == PLAYER_COMMAND_SELECT_SOURCE);
    assert(command.player.source == AUDIO_SOURCE_INTERNET_RADIO);

    const char *station =
        "{\"type\":\"command\",\"id\":\"43\",\"action\":\"list.select\",\"index\":3}";
    assert(parse(station, &command) == WEB_PROTOCOL_OK);
    assert(command.kind == WEB_COMMAND_PLAYER);
    assert(command.player.kind == PLAYER_COMMAND_SELECT_ITEM);
    assert(command.player.item_index == 3U);
}

static void test_accepts_bounded_wifi_credentials(void)
{
    web_command_t command;
    const char *wifi =
        "{\"type\":\"command\",\"id\":\"44\",\"action\":\"wifi.save\","
        "\"ssid\":\"home\",\"password\":\"secret\"}";
    assert(parse(wifi, &command) == WEB_PROTOCOL_OK);
    assert(command.kind == WEB_COMMAND_WIFI_SAVE);
    assert(strcmp(command.request_id, "44") == 0);
    assert(strcmp(command.wifi.ssid, "home") == 0);
    assert(strcmp(command.wifi.password, "secret") == 0);

    const char *open_wifi =
        "{\"type\":\"command\",\"id\":\"45\",\"action\":\"wifi.save\","
        "\"ssid\":\"open-network\",\"password\":\"\"}";
    assert(parse(open_wifi, &command) == WEB_PROTOCOL_OK);
    assert(strcmp(command.wifi.password, "") == 0);
}

static void test_rejects_bad_envelope_and_exact_schema_violations(void)
{
    const char *invalid[] = {
        "{}",
        "[]",
        "{\"type\":\"event\",\"id\":\"1\",\"action\":\"player.toggle\"}",
        "{\"type\":\"command\",\"action\":\"player.toggle\"}",
        "{\"type\":\"command\",\"id\":\"1\"}",
        "{\"type\":\"command\",\"id\":1,\"action\":\"player.toggle\"}",
        "{\"type\":\"command\",\"id\":\"1\",\"action\":false}",
        "{\"type\":\"command\",\"id\":\"1\",\"action\":\"unknown\"}",
        "{\"type\":\"command\",\"id\":\"1\",\"action\":\"player.toggle\",\"extra\":0}",
        "{\"type\":\"command\",\"type\":\"command\",\"id\":\"1\",\"action\":\"player.toggle\"}",
        "{\"type\":\"command\",\"id\":\"1\",\"id\":\"2\",\"action\":\"player.toggle\"}",
        "{\"type\":\"command\",\"id\":\"1\",\"action\":\"player.toggle\",\"action\":\"player.play\"}",
        "{\"type\":\"command\",\"id\":\"1\",\"action\":\"player.toggle\",\"source\":\"internet_radio\"}",
        "{\"type\":\"command\",\"id\":\"1\",\"action\":\"source.select\"}",
        "{\"type\":\"command\",\"id\":\"1\",\"action\":\"source.select\",\"source\":\"bluetooth\"}",
        "{\"type\":\"command\",\"id\":\"1\",\"action\":\"source.select\",\"source\":3}",
        "{\"type\":\"command\",\"id\":\"1\",\"action\":\"list.select\"}",
        "{\"type\":\"command\",\"id\":\"1\",\"action\":\"list.select\",\"index\":0,\"index\":1}",
        "{\"type\":\"command\",\"id\":\"1\",\"action\":\"wifi.save\",\"ssid\":\"home\"}",
        "{\"type\":\"command\",\"id\":\"1\",\"action\":\"wifi.save\",\"ssid\":\"home\",\"password\":\"\",\"index\":0}",
        "{\"type\":\"command\",\"id\":\"1\",\"action\":\"wifi.save\",\"ssid\":\"home\",\"ssid\":\"other\",\"password\":\"\"}",
    };

    for (size_t index = 0; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        assert_invalid_and_zeroed(invalid[index]);
    }
}

static void test_rejects_invalid_request_ids(void)
{
    const char *invalid_ids[] = {"", "with space", "slash/", "quote\\\"", "кириллица"};
    char json[180];
    for (size_t index = 0; index < sizeof(invalid_ids) / sizeof(invalid_ids[0]); ++index) {
        const int written = snprintf(json, sizeof(json),
                                     "{\"type\":\"command\",\"id\":\"%s\","
                                     "\"action\":\"player.toggle\"}", invalid_ids[index]);
        assert(written > 0 && (size_t)written < sizeof(json));
        assert_invalid_and_zeroed(json);
    }

    assert_invalid_and_zeroed(
        "{\"type\":\"command\",\"id\":\"123456789012345678901234567890123\","
        "\"action\":\"player.toggle\"}");
}

static void test_rejects_invalid_station_indexes(void)
{
    const char *invalid[] = {
        "{\"type\":\"command\",\"id\":\"1\",\"action\":\"list.select\",\"index\":-1}",
        "{\"type\":\"command\",\"id\":\"1\",\"action\":\"list.select\",\"index\":1.5}",
        "{\"type\":\"command\",\"id\":\"1\",\"action\":\"list.select\",\"index\":\"1\"}",
        "{\"type\":\"command\",\"id\":\"1\",\"action\":\"list.select\",\"index\":4294967296}",
        "{\"type\":\"command\",\"id\":\"1\",\"action\":\"list.select\",\"index\":1e-9999}",
        "{\"type\":\"command\",\"id\":\"1\",\"action\":\"list.select\",\"index\":4294967295.0000001}",
        "{\"type\":\"command\",\"id\":\"1\",\"action\":\"list.select\",\"index\":1.}",
        "{\"type\":\"command\",\"id\":\"1\",\"action\":\"list.select\",\"index\":1.e2}",
        "{\"type\":\"command\",\"id\":\"1\",\"action\":\"list.select\",\"index\":01}",
    };
    for (size_t index = 0; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        assert_invalid_and_zeroed(invalid[index]);
    }

    web_command_t command;
    const char *maximum =
        "{\"type\":\"command\",\"id\":\"max\",\"action\":\"list.select\","
        "\"index\":4294967295}";
    assert(parse(maximum, &command) == WEB_PROTOCOL_OK);
    assert(command.player.item_index == UINT32_MAX);
}

static void test_accepts_semantically_encoded_index_keys(void)
{
    web_command_t command;
    const char *encoded_first =
        "{\"type\":\"command\",\"id\":\"encoded-1\",\"action\":\"list.select\","
        "\"\\u0069ndex\":17}";
    assert(parse(encoded_first, &command) == WEB_PROTOCOL_OK);
    assert(command.player.item_index == 17U);

    const char *encoded_middle =
        "{\"type\":\"command\",\"id\":\"encoded-2\",\"action\":\"list.select\","
        "\"ind\\u0065x\":23}";
    assert(parse(encoded_middle, &command) == WEB_PROTOCOL_OK);
    assert(command.player.item_index == 23U);
}

static void test_rejects_semantic_index_duplicates_and_bad_key_escapes(void)
{
    assert_invalid_and_zeroed(
        "{\"type\":\"command\",\"id\":\"duplicate\",\"action\":\"list.select\","
        "\"index\":1,\"ind\\u0065x\":2}");
    assert_invalid_and_zeroed(
        "{\"type\":\"command\",\"id\":\"bad-high\",\"action\":\"list.select\","
        "\"index\":1,\"\\uD800\":0}");
    assert_invalid_and_zeroed(
        "{\"type\":\"command\",\"id\":\"bad-low\",\"action\":\"list.select\","
        "\"index\":1,\"\\uDC00\":0}");
    assert_invalid_and_zeroed(
        "{\"type\":\"command\",\"id\":\"bad-hex\",\"action\":\"list.select\","
        "\"index\":1,\"\\u00G0\":0}");
}

static void test_rejects_excessive_depth_before_cjson(void)
{
    char deeply_nested[WEB_PROTOCOL_FRAME_MAX + 1U];
    const size_t nesting = 250U;
    for (size_t index = 0U; index < nesting; ++index) {
        deeply_nested[index] = '[';
        deeply_nested[nesting + index] = ']';
    }
    deeply_nested[nesting * 2U] = '\0';

    const cJSON_Hooks hooks = {
        .malloc_fn = counting_malloc,
        .free_fn = counting_free,
    };
    s_cjson_allocations = 0U;
    cJSON_InitHooks((cJSON_Hooks *)&hooks);
    web_command_t command;
    memset(&command, 0xA5, sizeof(command));
    assert(parse(deeply_nested, &command) == WEB_PROTOCOL_INVALID);
    assert_zeroed(&command);
    assert(s_cjson_allocations == 0U);
    cJSON_InitHooks(NULL);
}

static void test_depth_scanner_ignores_brackets_inside_strings(void)
{
    web_command_t command;
    const char *json =
        "{\"type\":\"command\",\"id\":\"braces\",\"action\":\"wifi.save\","
        "\"ssid\":\"test\",\"password\":\"slash\\\\{[\\\"still text\\\"]}\"}";
    assert(parse(json, &command) == WEB_PROTOCOL_OK);
    assert(strcmp(command.wifi.password, "slash\\{[\"still text\"]}") == 0);
    web_protocol_clear_command(&command);
}

static void test_rejects_invalid_text_and_credential_lengths(void)
{
    assert_invalid_and_zeroed(
        "{\"type\":\"command\",\"id\":\"1\",\"action\":\"wifi.save\","
        "\"ssid\":\"\",\"password\":\"\"}");
    assert_invalid_and_zeroed(
        "{\"type\":\"command\",\"id\":\"1\",\"action\":\"wifi.save\","
        "\"ssid\":\"123456789012345678901234567890123\",\"password\":\"\"}");
    assert_invalid_and_zeroed(
        "{\"type\":\"command\",\"id\":\"1\",\"action\":\"wifi.save\","
        "\"ssid\":\"home\",\"password\":\"12345678901234567890123456789012345678901234567890123456789012345\"}");
    assert_invalid_and_zeroed(
        "{\"type\":\"command\",\"id\":\"1\",\"action\":\"wifi.save\","
        "\"ssid\":\"bad\\nname\",\"password\":\"\"}");
    assert_invalid_and_zeroed(
        "{\"type\":\"command\",\"id\":\"1\",\"action\":\"wifi.save\","
        "\"ssid\":\"bad\\u0000name\",\"password\":\"\"}");

    static const char invalid_utf8[] =
        "{\"type\":\"command\",\"id\":\"1\",\"action\":\"wifi.save\","
        "\"ssid\":\"bad\xC0\xAF\",\"password\":\"\"}";
    web_command_t command;
    memset(&command, 0xA5, sizeof(command));
    assert(web_protocol_parse_command(invalid_utf8, sizeof(invalid_utf8) - 1U, &command) ==
           WEB_PROTOCOL_INVALID);
    assert_zeroed(&command);
}

static void test_enforces_frame_bound_and_zeroes_all_failures(void)
{
    char oversized[WEB_PROTOCOL_FRAME_MAX + 2U];
    memset(oversized, 'x', sizeof(oversized));
    oversized[sizeof(oversized) - 1U] = '\0';

    web_command_t command;
    memset(&command, 0xA5, sizeof(command));
    assert(web_protocol_parse_command(oversized, WEB_PROTOCOL_FRAME_MAX + 1U, &command) ==
           WEB_PROTOCOL_TOO_LARGE);
    assert_zeroed(&command);

    memset(&command, 0xA5, sizeof(command));
    assert(web_protocol_parse_command(NULL, 0U, &command) == WEB_PROTOCOL_INVALID);
    assert_zeroed(&command);
}

int main(void)
{
    test_accepts_exact_player_commands();
    test_accepts_source_and_station_selection();
    test_accepts_bounded_wifi_credentials();
    test_rejects_bad_envelope_and_exact_schema_violations();
    test_rejects_invalid_request_ids();
    test_rejects_excessive_depth_before_cjson();
    test_depth_scanner_ignores_brackets_inside_strings();
    test_rejects_invalid_station_indexes();
    test_accepts_semantically_encoded_index_keys();
    test_rejects_semantic_index_duplicates_and_bad_key_escapes();
    test_rejects_invalid_text_and_credential_lengths();
    test_enforces_frame_bound_and_zeroes_all_failures();
    puts("web_protocol tests passed");
    return 0;
}
