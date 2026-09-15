#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "icy_metadata.h"

typedef struct {
    char title[ICY_METADATA_TITLE_MAX_LEN];
    unsigned int calls;
} title_capture_t;

static void capture_title(void *context, const char *title)
{
    title_capture_t *capture = context;

    capture->calls++;
    snprintf(capture->title, sizeof(capture->title), "%s", title);
}

static void test_passthrough_without_metadata(void)
{
    icy_metadata_t parser;
    uint8_t audio[8] = {0};
    size_t audio_length = 0;
    const uint8_t input[] = {1, 2, 3, 4, 5};

    icy_metadata_init(&parser, 0, NULL, NULL);
    assert(icy_metadata_feed(&parser, input, sizeof(input), audio, sizeof(audio),
                             &audio_length) == ICY_METADATA_OK);
    assert(audio_length == sizeof(input));
    assert(memcmp(audio, input, sizeof(input)) == 0);
}

static void test_extracts_title_across_network_chunks(void)
{
    icy_metadata_t parser;
    title_capture_t capture = {0};
    uint8_t audio[16] = {0};
    size_t audio_length = 0;
    const uint8_t first_chunk[] = {0x11, 0x22, 0x33, 2, 'S', 't', 'r', 'e', 'a'};
    const uint8_t second_chunk[] = {
        'm', 'T', 'i', 't', 'l', 'e', '=', '\'', 'A', 'r', 't', 'i', 's', 't',
        ' ', '-', ' ', 'T', 'r', 'a', 'c', 'k', '\'', ';', 0, 0, 0,
        0x44, 0x55, 0x66,
    };

    icy_metadata_init(&parser, 3, capture_title, &capture);

    assert(icy_metadata_feed(&parser, first_chunk, sizeof(first_chunk), audio,
                             sizeof(audio), &audio_length) == ICY_METADATA_OK);
    assert(audio_length == 3);
    assert(memcmp(audio, first_chunk, 3) == 0);
    assert(capture.calls == 0);

    assert(icy_metadata_feed(&parser, second_chunk, sizeof(second_chunk), audio,
                             sizeof(audio), &audio_length) == ICY_METADATA_OK);
    assert(audio_length == 3);
    assert(audio[0] == 0x44 && audio[1] == 0x55 && audio[2] == 0x66);
    assert(capture.calls == 1);
    assert(strcmp(capture.title, "Artist - Track") == 0);
}

static void test_empty_metadata_keeps_audio_alignment(void)
{
    icy_metadata_t parser;
    uint8_t audio[8] = {0};
    size_t audio_length = 0;
    const uint8_t input[] = {0xa1, 0xa2, 0, 0xa3, 0xa4};

    icy_metadata_init(&parser, 2, NULL, NULL);
    assert(icy_metadata_feed(&parser, input, sizeof(input), audio, sizeof(audio),
                             &audio_length) == ICY_METADATA_OK);
    assert(audio_length == 4);
    assert(audio[0] == 0xa1 && audio[1] == 0xa2);
    assert(audio[2] == 0xa3 && audio[3] == 0xa4);
}

/* What one server put where the song should be: its backend's JSON reply
 * and a chunked terminator. That is not a title, and the line ends are not
 * part of one either. */
static void test_a_json_title_is_no_title(void)
{
    icy_metadata_t parser;
    title_capture_t capture = {0};
    uint8_t audio[16] = {0};
    size_t audio_length = 0;
    static const char block[] =
        "StreamTitle='{\"status\":1,\"message\":\"Ok\",\"result\":\"Ok\",\"errorCode\":0}\r\n0\r\n\r\n';";
    /* One audio byte, then the block: the interval is 1. */
    uint8_t input[2 + 5 * 16];
    memset(input, 0, sizeof(input));
    input[0] = 0xAA;
    input[1] = 5;
    memcpy(&input[2], block, sizeof(block) - 1U);

    icy_metadata_init(&parser, 1, capture_title, &capture);
    assert(icy_metadata_feed(&parser, input, sizeof(input), audio, sizeof(audio),
                             &audio_length) == ICY_METADATA_OK);
    assert(capture.calls == 1);
    assert(capture.title[0] == '\0');

    /* A real title with a stray line end keeps its words. */
    static const char ok[] = "StreamTitle='Artist - Track\r\n';";
    uint8_t input2[2 + 2 * 16];
    memset(input2, 0, sizeof(input2));
    input2[0] = 0xAA;
    input2[1] = 2;
    memcpy(&input2[2], ok, sizeof(ok) - 1U);
    icy_metadata_init(&parser, 1, capture_title, &capture);
    assert(icy_metadata_feed(&parser, input2, sizeof(input2), audio, sizeof(audio),
                             &audio_length) == ICY_METADATA_OK);
    assert(strcmp(capture.title, "Artist - Track") == 0);
}

int main(void)
{
    test_passthrough_without_metadata();
    test_extracts_title_across_network_chunks();
    test_a_json_title_is_no_title();
    test_empty_metadata_keeps_audio_alignment();
    puts("icy_metadata tests passed");
    return 0;
}
