#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ui_radio_text.h"

static void test_unknown_bitrate_keeps_reported_codec(void)
{
    char text[32];

    ui_radio_stream_text(text, sizeof(text), "AAC", 0);
    assert(strcmp(text, "AAC  |  -- kbps") == 0);
}

static void test_known_bitrate_includes_codec(void)
{
    char text[32];

    ui_radio_stream_text(text, sizeof(text), "MP3", 128);
    assert(strcmp(text, "MP3  |  128 kbps") == 0);
}

static void test_station_url_selects_flac_label(void)
{
    char text[32];

    ui_radio_stream_text_for_url(text, sizeof(text),
                                 "http://stream.radioparadise.com/eclectic-flac");
    assert(strcmp(text, "FLAC  |  -- kbps") == 0);
}

int main(void)
{
    test_unknown_bitrate_keeps_reported_codec();
    test_known_bitrate_includes_codec();
    test_station_url_selects_flac_label();
    puts("ui_radio_text tests passed");
    return 0;
}
