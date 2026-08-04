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

int main(void)
{
    test_passthrough_without_metadata();
    test_extracts_title_across_network_chunks();
    test_empty_metadata_keeps_audio_alignment();
    puts("icy_metadata tests passed");
    return 0;
}
