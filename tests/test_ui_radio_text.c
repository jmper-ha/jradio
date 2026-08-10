#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ui_radio_text.h"

static void test_unknown_bitrate_keeps_reported_codec(void)
{
    char text[48];

    ui_radio_stream_text(text, sizeof(text), "AAC", 0, 0);
    assert(strcmp(text, "AAC  |  -- kbps  |  --") == 0);
}

static void test_known_bitrate_includes_codec(void)
{
    char text[48];

    ui_radio_stream_text(text, sizeof(text), "MP3", 128, 44100);
    assert(strcmp(text, "MP3  |  128 kbps  |  44100") == 0);
}

static void test_sample_rate_formatting(void)
{
    char text[48];

    /* The rate is shown verbatim in Hz, whatever the value. */
    ui_radio_stream_text(text, sizeof(text), "AAC", 320, 48000);
    assert(strcmp(text, "AAC  |  320 kbps  |  48000") == 0);
    ui_radio_stream_text(text, sizeof(text), "MP3", 96, 32000);
    assert(strcmp(text, "MP3  |  96 kbps  |  32000") == 0);

    /* Including rates that are not a whole number of kHz. */
    ui_radio_stream_text(text, sizeof(text), "FLAC", 1441, 44100);
    assert(strcmp(text, "FLAC  |  1441 kbps  |  44100") == 0);
    ui_radio_stream_text(text, sizeof(text), "MP3", 64, 22050);
    assert(strcmp(text, "MP3  |  64 kbps  |  22050") == 0);

    /* A rate known before the bitrate is, and vice versa. */
    ui_radio_stream_text(text, sizeof(text), "AAC", 0, 48000);
    assert(strcmp(text, "AAC  |  -- kbps  |  48000") == 0);
    ui_radio_stream_text(text, sizeof(text), "AAC", 128, 0);
    assert(strcmp(text, "AAC  |  128 kbps  |  --") == 0);
}

static void test_missing_codec_falls_back(void)
{
    char text[48];

    ui_radio_stream_text(text, sizeof(text), NULL, 0, 0);
    assert(strcmp(text, "--  |  -- kbps  |  --") == 0);
    ui_radio_stream_text(text, sizeof(text), "", 128, 44100);
    assert(strcmp(text, "--  |  128 kbps  |  44100") == 0);
}

static void test_station_url_selects_flac_label(void)
{
    char text[48];

    ui_radio_stream_text_for_url(text, sizeof(text),
                                 "http://stream.radioparadise.com/eclectic-flac");
    assert(strcmp(text, "FLAC  |  -- kbps  |  --") == 0);
}

int main(void)
{
    test_unknown_bitrate_keeps_reported_codec();
    test_known_bitrate_includes_codec();
    test_sample_rate_formatting();
    test_missing_codec_falls_back();
    test_station_url_selects_flac_label();
    puts("ui_radio_text tests passed");
    return 0;
}
