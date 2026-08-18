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


static void test_an_icy_title_splits_into_performer_and_track(void)
{
    /* The real string this device receives, from the log of a live station. */
    char artist[64];
    char title[64];
    assert(ui_radio_split_title("DJ JEDY / Kate Linch / Niki Four - Supergirl (by Reamonn)",
                                artist, sizeof(artist), title, sizeof(title)));
    assert(strcmp(artist, "DJ JEDY / Kate Linch / Niki Four") == 0);
    assert(strcmp(title, "Supergirl (by Reamonn)") == 0);
}

static void test_only_a_spaced_dash_counts_as_a_separator(void)
{
    /* A hyphenated name has no spaces around its dash, and splitting there
     * would cut a word in half. */
    char artist[64];
    char title[64];
    assert(!ui_radio_split_title("Jean-Michel Jarre", artist, sizeof(artist), title,
                                 sizeof(title)));
    assert(artist[0] == '\0');
    assert(strcmp(title, "Jean-Michel Jarre") == 0);

    /* Only the first separator splits: everything after it belongs to the
     * track, dashes included. */
    assert(ui_radio_split_title("A - B - C", artist, sizeof(artist), title, sizeof(title)));
    assert(strcmp(artist, "A") == 0);
    assert(strcmp(title, "B - C") == 0);
}

static void test_a_lopsided_or_missing_title_is_left_whole(void)
{
    char artist[64];
    char title[64];

    /* Nothing before the dash, or nothing after it: not a performer and a
     * track, just a string that happens to contain a dash. */
    assert(!ui_radio_split_title(" - Supergirl", artist, sizeof(artist), title, sizeof(title)));
    assert(strcmp(title, " - Supergirl") == 0);
    assert(!ui_radio_split_title("Supergirl - ", artist, sizeof(artist), title, sizeof(title)));
    assert(strcmp(title, "Supergirl - ") == 0);

    assert(!ui_radio_split_title("", artist, sizeof(artist), title, sizeof(title)));
    assert(title[0] == '\0');
    assert(!ui_radio_split_title(NULL, artist, sizeof(artist), title, sizeof(title)));
    assert(title[0] == '\0');
    assert(!ui_radio_split_title("A - B", NULL, sizeof(artist), title, sizeof(title)));
    assert(!ui_radio_split_title("A - B", artist, 0U, title, sizeof(title)));
}

static void test_a_performer_that_does_not_fit_is_not_truncated(void)
{
    /* Half a name attributed to the wrong artist is worse than showing the
     * station's string as it came. */
    char artist[8];
    char title[64];
    assert(!ui_radio_split_title("A very long performer name - Track", artist, sizeof(artist),
                                 title, sizeof(title)));
    assert(artist[0] == '\0');
    assert(strcmp(title, "A very long performer name - Track") == 0);
}

int main(void)
{
    test_unknown_bitrate_keeps_reported_codec();
    test_known_bitrate_includes_codec();
    test_sample_rate_formatting();
    test_missing_codec_falls_back();
    test_station_url_selects_flac_label();
    test_an_icy_title_splits_into_performer_and_track();
    test_only_a_spaced_dash_counts_as_a_separator();
    test_a_lopsided_or_missing_title_is_left_whole();
    test_a_performer_that_does_not_fit_is_not_truncated();
    puts("ui_radio_text tests passed");
    return 0;
}
