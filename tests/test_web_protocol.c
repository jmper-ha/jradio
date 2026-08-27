#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "web_protocol.h"

static size_t s_cjson_allocations;
static bool s_secret_reached_free;
static size_t s_fault_allocation_count;
static size_t s_fault_fail_at;

typedef struct {
    size_t size;
} tracked_allocation_t;

static bool contains_bytes(const unsigned char *memory, size_t size,
                           const char *needle)
{
    const size_t needle_size = strlen(needle);
    if (needle_size == 0U || needle_size > size) return false;
    for (size_t offset = 0U; offset <= size - needle_size; ++offset) {
        if (memcmp(memory + offset, needle, needle_size) == 0) return true;
    }
    return false;
}

static void *secret_tracking_malloc(size_t size)
{
    tracked_allocation_t *allocation = malloc(sizeof(*allocation) + size);
    assert(allocation != NULL);
    allocation->size = size;
    memset(allocation + 1, 0, size);
    return allocation + 1;
}

static void secret_tracking_free(void *memory)
{
    if (memory == NULL) return;
    tracked_allocation_t *allocation =
        ((tracked_allocation_t *)memory) - 1;
    if (contains_bytes(memory, allocation->size, "topsecret42")) {
        s_secret_reached_free = true;
    }
    free(allocation);
}

static void *fault_tracking_malloc(size_t size)
{
    ++s_fault_allocation_count;
    if (s_fault_allocation_count == s_fault_fail_at) return NULL;
    tracked_allocation_t *allocation = malloc(sizeof(*allocation) + size);
    if (allocation == NULL) return NULL;
    allocation->size = size;
    memset(allocation + 1, 0, size);
    return allocation + 1;
}

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

    const char *with_json_whitespace =
        "{\"type\":\"command\",\"id\":\"space\","
        "\"action\":\"player.play\"}\r\n\t ";
    assert(parse(with_json_whitespace, &command) == WEB_PROTOCOL_OK);
    assert(command.player.kind == PLAYER_COMMAND_PLAY);
}

/* Skipping a track carries nothing but the action: which track is playing is
 * the device's business, and an index here would be a second opinion about it
 * that could already be stale by the time it arrives. */
static void test_accepts_the_next_track_command(void)
{
    web_command_t command;
    const char *next = "{\"type\":\"command\",\"id\":\"n1\",\"action\":\"player.next\"}";
    assert(parse(next, &command) == WEB_PROTOCOL_OK);
    assert(command.kind == WEB_COMMAND_PLAYER);
    assert(command.player.kind == PLAYER_COMMAND_NEXT_TRACK);

    const char *extra =
        "{\"type\":\"command\",\"id\":\"n2\",\"action\":\"player.next\",\"index\":1}";
    assert(parse(extra, &command) != WEB_PROTOCOL_OK);
}

/* The like mark carries nothing either, and for a stronger reason: it toggles
 * whatever the device currently holds, so a client that named a state would be
 * telling the device what it already knows - and telling it wrong whenever a
 * second press raced the answer to the first. */
static void test_accepts_the_like_command(void)
{
    web_command_t command;
    const char *like = "{\"type\":\"command\",\"id\":\"l1\",\"action\":\"player.like\"}";
    assert(parse(like, &command) == WEB_PROTOCOL_OK);
    assert(command.kind == WEB_COMMAND_PLAYER);
    assert(command.player.kind == PLAYER_COMMAND_TOGGLE_LIKE);

    const char *extra =
        "{\"type\":\"command\",\"id\":\"l2\",\"action\":\"player.like\",\"index\":1}";
    assert(parse(extra, &command) != WEB_PROTOCOL_OK);
}

/* The rejection is its own action rather than a field on the one above: the
 * browser has a button per mark and the device has one key, so a shared "set
 * this mark" would have to say which one anyway. */
static void test_accepts_the_dislike_command(void)
{
    web_command_t command;
    const char *dislike = "{\"type\":\"command\",\"id\":\"d1\",\"action\":\"player.dislike\"}";
    assert(parse(dislike, &command) == WEB_PROTOCOL_OK);
    assert(command.kind == WEB_COMMAND_PLAYER);
    assert(command.player.kind == PLAYER_COMMAND_TOGGLE_DISLIKE);

    const char *extra =
        "{\"type\":\"command\",\"id\":\"d2\",\"action\":\"player.dislike\",\"index\":0}";
    assert(parse(extra, &command) != WEB_PROTOCOL_OK);
}

/* The two track keys carry nothing either. They are not the same command as
 * player.next above: this pair steps along the list the playing item came
 * from, while that one asks the rotor for another track - and the rotor is the
 * only source that has one to give. */
static void test_accepts_the_track_keys(void)
{
    web_command_t command;
    const char *previous =
        "{\"type\":\"command\",\"id\":\"p1\",\"action\":\"player.previous_item\"}";
    assert(parse(previous, &command) == WEB_PROTOCOL_OK);
    assert(command.kind == WEB_COMMAND_PLAYER);
    assert(command.player.kind == PLAYER_COMMAND_PREVIOUS_ITEM);

    const char *next =
        "{\"type\":\"command\",\"id\":\"p2\",\"action\":\"player.next_item\"}";
    assert(parse(next, &command) == WEB_PROTOCOL_OK);
    assert(command.player.kind == PLAYER_COMMAND_NEXT_ITEM);

    /* No index: which item is playing is the device's business, and one named
     * here could already be stale - the listing is re-read as folders are
     * opened. */
    const char *extra =
        "{\"type\":\"command\",\"id\":\"p3\",\"action\":\"player.next_item\","
        "\"index\":1}";
    assert(parse(extra, &command) != WEB_PROTOCOL_OK);
}

/* A jump names the second to land on and nothing else. Seconds rather than a
 * percentage, because seconds are what the device turns into a byte offset -
 * and the number is read off the frame rather than out of cJSON, which keeps
 * every number as a double. */
static void test_accepts_the_seek_command(void)
{
    web_command_t command;
    const char *seek =
        "{\"type\":\"command\",\"id\":\"s1\",\"action\":\"player.seek\","
        "\"position\":137}";
    assert(parse(seek, &command) == WEB_PROTOCOL_OK);
    assert(command.kind == WEB_COMMAND_PLAYER);
    assert(command.player.kind == PLAYER_COMMAND_SEEK);
    assert(command.player.position_seconds == 137U);
    // The row index is a different quantity and must not be carried along.
    assert(command.player.item_index == PLAYER_ITEM_NONE);

    const char *start =
        "{\"type\":\"command\",\"id\":\"s2\",\"action\":\"player.seek\","
        "\"position\":0}";
    assert(parse(start, &command) == WEB_PROTOCOL_OK);
    assert(command.player.position_seconds == 0U);

    // Without one there is nowhere to land.
    const char *bare =
        "{\"type\":\"command\",\"id\":\"s3\",\"action\":\"player.seek\"}";
    assert(parse(bare, &command) != WEB_PROTOCOL_OK);

    /* Not a whole second, not a negative one, and not a row index wearing the
     * other name: each of these would land somewhere nobody asked for. */
    const char *fractional =
        "{\"type\":\"command\",\"id\":\"s4\",\"action\":\"player.seek\","
        "\"position\":12.5}";
    assert(parse(fractional, &command) != WEB_PROTOCOL_OK);
    const char *negative =
        "{\"type\":\"command\",\"id\":\"s5\",\"action\":\"player.seek\","
        "\"position\":-1}";
    assert(parse(negative, &command) != WEB_PROTOCOL_OK);
    const char *indexed =
        "{\"type\":\"command\",\"id\":\"s6\",\"action\":\"player.seek\","
        "\"index\":12}";
    assert(parse(indexed, &command) != WEB_PROTOCOL_OK);

    /* And the position must not leak into the commands that carry no number:
     * the scanner reads the frame, not the action. */
    const char *positioned_toggle =
        "{\"type\":\"command\",\"id\":\"s7\",\"action\":\"player.toggle\","
        "\"position\":5}";
    assert(parse(positioned_toggle, &command) != WEB_PROTOCOL_OK);

    // A selection still carries its own number, unchanged by the new one.
    const char *select =
        "{\"type\":\"command\",\"id\":\"s8\",\"action\":\"list.select\","
        "\"index\":3}";
    assert(parse(select, &command) == WEB_PROTOCOL_OK);
    assert(command.player.kind == PLAYER_COMMAND_SELECT_ITEM);
    assert(command.player.item_index == 3U);
    assert(command.player.position_seconds == 0U);
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

    const char *usb =
        "{\"type\":\"command\",\"id\":\"source-2\","
        "\"action\":\"source.select\",\"source\":\"usb\"}";
    assert(parse(usb, &command) == WEB_PROTOCOL_OK);
    assert(command.kind == WEB_COMMAND_PLAYER);
    assert(command.player.kind == PLAYER_COMMAND_SELECT_SOURCE);
    assert(command.player.source == AUDIO_SOURCE_USB);

    const char *yandex =
        "{\"type\":\"command\",\"id\":\"source-3\","
        "\"action\":\"source.select\",\"source\":\"yandex\"}";
    assert(parse(yandex, &command) == WEB_PROTOCOL_OK);
    assert(command.kind == WEB_COMMAND_PLAYER);
    assert(command.player.kind == PLAYER_COMMAND_SELECT_SOURCE);
    assert(command.player.source == AUDIO_SOURCE_YANDEX);

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

    const char *escaped =
        "{\"type\":\"command\",\"id\":\"46\",\"action\":\"wifi.save\","
        "\"ssid\":\"unicode\","
        "\"password\":\"q\\\"\\\\\\/\\u2603\\uD83D\\uDE80\"}";
    assert(parse(escaped, &command) == WEB_PROTOCOL_OK);
    assert(strcmp(command.wifi.password, "q\"\\/☃🚀") == 0);
    web_protocol_clear_command(&command);

    const char *all_simple_escapes =
        "{\"type\":\"command\",\"id\":\"47\",\"action\":\"wifi.save\","
        "\"ssid\":\"simple\",\"password\":\"a\\\"b\\\\c\\/d\"}";
    assert(parse(all_simple_escapes, &command) == WEB_PROTOCOL_OK);
    assert(strcmp(command.wifi.password, "a\"b\\c/d") == 0);
    web_protocol_clear_command(&command);

    const char *semantic_key_and_cyrillic =
        "{\"type\":\"command\",\"id\":\"48\",\"action\":\"wifi.save\","
        "\"ssid\":\"unicode\",\"pass\\u0077ord\":"
        "\"\\u041F\\u0430\\u0440\\u043E\\u043B\\u044C\\uD83D\\uDE00\"}";
    assert(parse(semantic_key_and_cyrillic, &command) == WEB_PROTOCOL_OK);
    assert(strcmp(command.wifi.password, "Пароль😀") == 0);
    web_protocol_clear_command(&command);
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

    char decoded_too_long[WEB_PROTOCOL_FRAME_MAX + 1U];
    size_t length = (size_t)snprintf(
        decoded_too_long, sizeof(decoded_too_long),
        "{\"type\":\"command\",\"id\":\"long\",\"action\":\"wifi.save\","
        "\"ssid\":\"home\",\"password\":\"");
    for (size_t index = 0U; index < 65U; ++index) {
        assert(length + 6U < sizeof(decoded_too_long));
        memcpy(decoded_too_long + length, "\\u0061", 6U);
        length += 6U;
    }
    memcpy(decoded_too_long + length, "\"}", 3U);
    assert_invalid_and_zeroed(decoded_too_long);

    assert_invalid_and_zeroed(
        "{\"type\":\"command\",\"id\":\"duplicate-secret\","
        "\"action\":\"wifi.save\",\"ssid\":\"home\","
        "\"password\":\"one\",\"pass\\u0077ord\":\"two\"}");
    assert_invalid_and_zeroed(
        "{\"type\":\"command\",\"id\":\"lone-high\","
        "\"action\":\"wifi.save\",\"ssid\":\"home\","
        "\"password\":\"\\uD800\"}");
    assert_invalid_and_zeroed(
        "{\"type\":\"command\",\"id\":\"lone-low\","
        "\"action\":\"wifi.save\",\"ssid\":\"home\","
        "\"password\":\"\\uDC00\"}");
    assert_invalid_and_zeroed(
        "{\"type\":\"command\",\"id\":\"control\","
        "\"action\":\"wifi.save\",\"ssid\":\"home\","
        "\"password\":\"bad\\nsecret\"}");
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

static void test_wipes_nested_passwords_on_all_parsed_error_paths(void)
{
    const cJSON_Hooks hooks = {
        .malloc_fn = secret_tracking_malloc,
        .free_fn = secret_tracking_free,
    };
    cJSON_InitHooks((cJSON_Hooks *)&hooks);

    const char *invalid[] = {
        "[{\"password\":\"topsecret42\"}]",
        "{\"outer\":{\"password\":\"topsecret42\"}} trailing",
        "{\"type\":\"command\",\"id\":\"1\",\"action\":\"unknown\","
        "\"extra\":{\"password\":\"topsecret42\"}}",
        "{\"type\":\"command\",\"id\":\"1\",\"action\":\"wifi.save\","
        "\"ssid\":\"x\",\"password\":\"topsecret42\",}",
        "{\"type\":\"command\",\"id\":\"1\",\"action\":\"wifi.save\","
        "\"ssid\":\"x\",\"password\":\"topsecret42\",\"\\uD800\":0}",
    };
    for (size_t index = 0U; index < sizeof(invalid) / sizeof(invalid[0]);
         ++index) {
        s_secret_reached_free = false;
        web_command_t command;
        memset(&command, 0xA5, sizeof(command));
        assert(parse(invalid[index], &command) == WEB_PROTOCOL_INVALID);
        assert_zeroed(&command);
        assert(!s_secret_reached_free);
    }
    cJSON_InitHooks(NULL);
}

static void test_cjson_oom_never_observes_real_password(void)
{
    const char *valid_json =
        "{\"type\":\"command\",\"id\":\"oom-ok\",\"action\":\"wifi.save\","
        "\"ssid\":\"home\",\"password\":\"topsecret42\"}";
    const char *invalid_nested_json =
        "{\"type\":\"command\",\"id\":\"oom\",\"action\":\"wifi.save\","
        "\"ssid\":\"home\",\"password\":\"topsecret42\","
        "\"extra\":{\"one\":\"1\",\"two\":\"2\",\"three\":\"3\"}}";
    const char *invalid_nonstring_password =
        "{\"type\":\"command\",\"id\":\"oom-object\","
        "\"action\":\"wifi.save\",\"ssid\":\"home\","
        "\"password\":{\"nested\":\"topsecret42\"}}";
    const cJSON_Hooks hooks = {
        .malloc_fn = fault_tracking_malloc,
        .free_fn = secret_tracking_free,
    };
    cJSON_InitHooks((cJSON_Hooks *)&hooks);

    const char *cases[] = {
        valid_json, invalid_nested_json, invalid_nonstring_password,
    };
    for (size_t case_index = 0U; case_index < 3U; ++case_index) {
        s_fault_allocation_count = 0U;
        s_fault_fail_at = SIZE_MAX;
        s_secret_reached_free = false;
        web_command_t command;
        const web_protocol_result_t baseline = parse(cases[case_index], &command);
        assert(baseline == (case_index == 0U ? WEB_PROTOCOL_OK :
                                                WEB_PROTOCOL_INVALID));
        web_protocol_clear_command(&command);
        const size_t successful_allocation_count = s_fault_allocation_count;
        assert(successful_allocation_count > 0U);
        assert(!s_secret_reached_free);

        for (size_t fail_at = 1U; fail_at <= successful_allocation_count;
             ++fail_at) {
            s_fault_allocation_count = 0U;
            s_fault_fail_at = fail_at;
            s_secret_reached_free = false;
            memset(&command, 0xA5, sizeof(command));
            assert(parse(cases[case_index], &command) == WEB_PROTOCOL_INVALID);
            assert_zeroed(&command);
            assert(!s_secret_reached_free);
        }
    }
    cJSON_InitHooks(NULL);
}

int main(void)
{
    test_accepts_exact_player_commands();
    test_accepts_source_and_station_selection();
    test_accepts_the_next_track_command();
    test_accepts_the_like_command();
    test_accepts_the_dislike_command();
    test_accepts_the_track_keys();
    test_accepts_the_seek_command();
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
    test_wipes_nested_passwords_on_all_parsed_error_paths();
    test_cjson_oom_never_observes_real_password();
    puts("web_protocol tests passed");
    return 0;
}
