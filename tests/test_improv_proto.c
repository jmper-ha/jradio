#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "improv_proto.h"

static bool feed_all(improv_decoder_t *decoder, const uint8_t *bytes, size_t length,
                     size_t *packets)
{
    bool last = false;
    for (size_t index = 0; index < length; ++index) {
        last = improv_decoder_feed(decoder, bytes[index]);
        if (last && packets != NULL) ++*packets;
    }
    return last;
}

/* An RPC frame the way a client builds one: command, length, payload. */
static size_t rpc_frame(uint8_t command, const uint8_t *payload, uint8_t length, uint8_t *out,
                        size_t capacity)
{
    uint8_t data[IMPROV_DATA_MAX];
    data[0] = command;
    data[1] = length;
    if (length > 0U) memcpy(data + 2, payload, length);
    return improv_frame(IMPROV_TYPE_RPC, data, 2U + length, out, capacity);
}

static void test_a_frame_has_the_header_the_checksum_and_a_newline(void)
{
    uint8_t out[IMPROV_FRAME_MAX];
    const size_t length = improv_frame_state(IMPROV_STATE_READY, out, sizeof(out));
    const uint8_t expected[] = {'I', 'M', 'P', 'R', 'O', 'V', 1, 0x01, 1, 0x02, 0, '\n'};
    assert(length == sizeof(expected));
    uint8_t sum = 0;
    for (size_t index = 0; index < 10U; ++index) sum = (uint8_t)(sum + out[index]);
    uint8_t want[sizeof(expected)];
    memcpy(want, expected, sizeof(expected));
    want[10] = sum;
    assert(memcmp(out, want, sizeof(want)) == 0);
    // Too small a buffer is an empty answer, never a cut frame.
    assert(improv_frame_state(IMPROV_STATE_READY, out, 11U) == 0U);
}

static void test_the_decoder_finds_packets_between_log_lines(void)
{
    uint8_t frame[IMPROV_FRAME_MAX];
    const size_t length = rpc_frame(IMPROV_COMMAND_GET_INFO, NULL, 0U, frame, sizeof(frame));
    uint8_t stream[256];
    size_t at = 0;
    const char *log = "I (123) wifi: connected\nIMPRO garbage I";
    memcpy(stream + at, log, strlen(log));
    at += strlen(log);
    memcpy(stream + at, frame, length);
    at += length;
    memcpy(stream + at, frame, length);
    at += length;

    improv_decoder_t decoder;
    improv_decoder_reset(&decoder);
    size_t packets = 0;
    (void)feed_all(&decoder, stream, at, &packets);
    assert(packets == 2U);
}

static void test_a_bad_checksum_or_version_is_dropped(void)
{
    uint8_t frame[IMPROV_FRAME_MAX];
    size_t length = rpc_frame(IMPROV_COMMAND_GET_STATE, NULL, 0U, frame, sizeof(frame));
    improv_decoder_t decoder;
    improv_decoder_reset(&decoder);
    frame[length - 2U] ^= 0x01U; // the checksum
    size_t packets = 0;
    (void)feed_all(&decoder, frame, length, &packets);
    assert(packets == 0U);

    length = rpc_frame(IMPROV_COMMAND_GET_STATE, NULL, 0U, frame, sizeof(frame));
    frame[6] = 2U; // a version this side does not speak
    (void)feed_all(&decoder, frame, length, &packets);
    assert(packets == 0U);

    // And the next good one still gets through.
    length = rpc_frame(IMPROV_COMMAND_GET_STATE, NULL, 0U, frame, sizeof(frame));
    (void)feed_all(&decoder, frame, length, &packets);
    assert(packets == 1U);
    assert(decoder.type == IMPROV_TYPE_RPC);
}

static void test_the_wifi_command_carries_the_network(void)
{
    const uint8_t payload[] = {4, 'H', 'o', 'm', 'e', 6, 's', 'e', 'c', 'r', 'e', 't'};
    uint8_t frame[IMPROV_FRAME_MAX];
    const size_t length =
        rpc_frame(IMPROV_COMMAND_WIFI_SETTINGS, payload, sizeof(payload), frame, sizeof(frame));
    improv_decoder_t decoder;
    improv_decoder_reset(&decoder);
    assert(feed_all(&decoder, frame, length - 1U, NULL)); // all but the newline
    improv_rpc_t rpc;
    assert(improv_rpc_parse(decoder.data, decoder.length, &rpc) == IMPROV_ERROR_NONE);
    assert(rpc.command == IMPROV_COMMAND_WIFI_SETTINGS);
    assert(strcmp(rpc.ssid, "Home") == 0);
    assert(strcmp(rpc.password, "secret") == 0);
    improv_decoder_reset(&decoder);
    for (size_t index = 0; index < sizeof(decoder.data); ++index) assert(decoder.data[index] == 0);

    // An open network: no password at all.
    const uint8_t open_net[] = {3, 'C', 'a', 'f', 0};
    const uint8_t data_open[] = {IMPROV_COMMAND_WIFI_SETTINGS, sizeof(open_net), 3, 'C', 'a', 'f', 0};
    assert(improv_rpc_parse(data_open, sizeof(data_open), &rpc) == IMPROV_ERROR_NONE);
    assert(strcmp(rpc.ssid, "Caf") == 0 && rpc.password[0] == '\0');
    (void)open_net;
}

static void test_a_wifi_command_that_does_not_add_up_is_refused(void)
{
    improv_rpc_t rpc;
    // The SSID runs past the payload.
    const uint8_t overrun[] = {IMPROV_COMMAND_WIFI_SETTINGS, 3, 9, 'a', 'b'};
    assert(improv_rpc_parse(overrun, sizeof(overrun), &rpc) == IMPROV_ERROR_INVALID_RPC);
    // An empty SSID.
    const uint8_t empty[] = {IMPROV_COMMAND_WIFI_SETTINGS, 2, 0, 0};
    assert(improv_rpc_parse(empty, sizeof(empty), &rpc) == IMPROV_ERROR_INVALID_RPC);
    // A NUL inside the password.
    const uint8_t nul[] = {IMPROV_COMMAND_WIFI_SETTINGS, 5, 1, 'x', 2, 'a', 0};
    assert(improv_rpc_parse(nul, sizeof(nul), &rpc) == IMPROV_ERROR_INVALID_RPC);
    assert(rpc.password[0] == '\0');
    // The length byte claims more than the packet holds.
    const uint8_t short_packet[] = {IMPROV_COMMAND_GET_STATE, 4};
    assert(improv_rpc_parse(short_packet, sizeof(short_packet), &rpc) == IMPROV_ERROR_INVALID_RPC);
    // A command this side does not have.
    const uint8_t unknown[] = {0x7F, 0};
    assert(improv_rpc_parse(unknown, sizeof(unknown), &rpc) == IMPROV_ERROR_UNKNOWN_RPC);
}

static void test_a_result_is_its_strings_behind_their_lengths(void)
{
    const char *info[] = {"jRadio", "1.6.0", "ESP32-S3", "jradio-B670"};
    uint8_t out[IMPROV_FRAME_MAX];
    const size_t length = improv_frame_result(IMPROV_COMMAND_GET_INFO, info, 4U, out, sizeof(out));
    assert(length > 0U);
    improv_decoder_t decoder;
    improv_decoder_reset(&decoder);
    assert(feed_all(&decoder, out, length - 1U, NULL)); // all but the newline
    assert(decoder.type == IMPROV_TYPE_RPC_RESULT);
    assert(decoder.data[0] == IMPROV_COMMAND_GET_INFO);
    assert(decoder.data[1] == decoder.length - 2U);
    assert(decoder.data[2] == 6U && memcmp(decoder.data + 3, "jRadio", 6) == 0);
    // The empty result that ends a network list: command and a zero length.
    const size_t empty = improv_frame_result(IMPROV_COMMAND_GET_NETWORKS, NULL, 0U, out, sizeof(out));
    assert(empty == 6U + 3U + 2U + 2U);
    assert(out[8] == 2U && out[9] == IMPROV_COMMAND_GET_NETWORKS && out[10] == 0U);
    // Too much for one packet is no packet.
    char big[200];
    memset(big, 'x', sizeof(big) - 1U);
    big[sizeof(big) - 1U] = '\0';
    const char *two[] = {big, big};
    assert(improv_frame_result(IMPROV_COMMAND_GET_INFO, two, 2U, out, sizeof(out)) == 0U);
}

int main(void)
{
    test_a_frame_has_the_header_the_checksum_and_a_newline();
    test_the_decoder_finds_packets_between_log_lines();
    test_a_bad_checksum_or_version_is_dropped();
    test_the_wifi_command_carries_the_network();
    test_a_wifi_command_that_does_not_add_up_is_refused();
    test_a_result_is_its_strings_behind_their_lengths();
    puts("improv_proto tests passed");
    return 0;
}
