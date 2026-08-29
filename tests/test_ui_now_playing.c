#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ui_now_playing.h"

static void test_an_icy_title_splits_into_performer_and_track(void)
{
    /* The real string this device receives, from the log of a live station. */
    char artist[64];
    char title[64];
    assert(ui_now_playing_split_title("DJ JEDY / Kate Linch / Niki Four - Supergirl (by Reamonn)",
                                      NULL, artist, sizeof(artist), title, sizeof(title)));
    assert(strcmp(artist, "DJ JEDY / Kate Linch / Niki Four") == 0);
    assert(strcmp(title, "Supergirl (by Reamonn)") == 0);
}

static void test_a_spaced_dash_of_either_kind_counts(void)
{
    /* A hyphenated name has no spaces around its dash, and splitting there
     * would cut a word in half. */
    char artist[64];
    char title[64];
    assert(!ui_now_playing_split_title("Jean-Michel Jarre", NULL, artist, sizeof(artist),
                                       title, sizeof(title)));
    assert(artist[0] == '\0');
    assert(strcmp(title, "Jean-Michel Jarre") == 0);

    /* Only the first separator splits: everything after it belongs to the
     * track, dashes included. */
    assert(ui_now_playing_split_title("A - B - C", NULL, artist, sizeof(artist), title,
                                      sizeof(title)));
    assert(strcmp(artist, "A") == 0);
    assert(strcmp(title, "B - C") == 0);

    /* European stations send an en dash where this one sends a hyphen, and it
     * is the same announcement. Whichever comes first wins. */
    assert(ui_now_playing_split_title("Artist \xE2\x80\x93 Song - Live", NULL, artist,
                                      sizeof(artist), title, sizeof(title)));
    assert(strcmp(artist, "Artist") == 0);
    assert(strcmp(title, "Song - Live") == 0);
}

static void test_a_lopsided_or_missing_title_is_left_whole(void)
{
    char artist[64];
    char title[64];

    /* Nothing before the dash, or nothing after it: not a performer and a
     * track, just a string that happens to contain a dash. The surrounding
     * space goes, because a station's padding is not part of the name. */
    assert(!ui_now_playing_split_title(" - Supergirl", NULL, artist, sizeof(artist), title,
                                       sizeof(title)));
    assert(strcmp(title, "- Supergirl") == 0);
    assert(!ui_now_playing_split_title("Supergirl - ", NULL, artist, sizeof(artist), title,
                                       sizeof(title)));
    assert(strcmp(title, "Supergirl -") == 0);
    assert(ui_now_playing_split_title("  Sam Feldt - Be My Lover  ", NULL, artist,
                                      sizeof(artist), title, sizeof(title)));
    assert(strcmp(artist, "Sam Feldt") == 0);
    assert(strcmp(title, "Be My Lover") == 0);

    assert(!ui_now_playing_split_title("", NULL, artist, sizeof(artist), title, sizeof(title)));
    assert(title[0] == '\0');
    assert(!ui_now_playing_split_title(NULL, NULL, artist, sizeof(artist), title,
                                       sizeof(title)));
    assert(title[0] == '\0');
    assert(!ui_now_playing_split_title("A - B", NULL, NULL, sizeof(artist), title,
                                       sizeof(title)));
    assert(!ui_now_playing_split_title("A - B", NULL, artist, 0U, title, sizeof(title)));
    // Neither output: nothing to write, and nothing to crash on either.
    ui_now_playing_split_title("ignored", NULL, NULL, 0U, NULL, 0U);
}

static void test_a_performer_that_does_not_fit_is_not_truncated(void)
{
    /* Half a name attributed to the wrong artist is worse than showing the
     * station's string as it came. */
    char artist[8];
    char title[64];
    assert(!ui_now_playing_split_title("A very long performer name - Track", NULL, artist,
                                       sizeof(artist), title, sizeof(title)));
    assert(artist[0] == '\0');
    assert(strcmp(title, "A very long performer name - Track") == 0);
}

static void test_a_title_that_only_repeats_the_line_above_is_not_split(void)
{
    /* A station announcing itself rather than a track. Split, "Jazz - Lounge"
     * would be filed under a performer called Jazz. */
    char artist[64];
    char title[64];
    assert(!ui_now_playing_split_title("Jazz - Lounge", "Jazz - Lounge", artist,
                                       sizeof(artist), title, sizeof(title)));
    assert(artist[0] == '\0');
    assert(strcmp(title, "Jazz - Lounge") == 0);
    // A different heading leaves the rule where it was.
    assert(ui_now_playing_split_title("Jazz - Lounge", "Радио Шоколад", artist,
                                      sizeof(artist), title, sizeof(title)));
}

static void test_what_a_station_sent_is_made_renderable(void)
{
    /* ICY bytes arrive as whatever the station felt like sending, and both
     * faces have to survive them: the page as valid JSON, the panel as a
     * string LVGL can walk. */
    char artist[64];
    char title[64];
    static const char surrogate[] = {'A', (char)0xEDU, (char)0xA0U, (char)0x80U, '\0'};
    static const char overlong[] = {'A', (char)0xC0U, (char)0xAFU, '\0'};
    static const char above_unicode[] = {'A', (char)0xF4U, (char)0x90U, (char)0x80U,
                                         (char)0x80U, '\0'};
    /* One mark for the whole bad sequence, not one per byte: what was wrong is
     * the character, and three marks would read as three lost letters. */
    ui_now_playing_split_title(surrogate, NULL, artist, sizeof(artist), title, sizeof(title));
    assert(strcmp(title, "A\xEF\xBF\xBD") == 0);
    ui_now_playing_split_title(overlong, NULL, artist, sizeof(artist), title, sizeof(title));
    assert(strcmp(title, "A\xEF\xBF\xBD") == 0);
    ui_now_playing_split_title(above_unicode, NULL, artist, sizeof(artist), title,
                               sizeof(title));
    assert(strcmp(title, "A\xEF\xBF\xBD") == 0);

    /* A whole character or none of it: half a sequence would be the same
     * broken byte the sanitising is here to remove. */
    char narrow[3];
    ui_now_playing_split_title("A\xE2\x82\xAC", NULL, artist, sizeof(artist), narrow,
                               sizeof(narrow));
    assert(strcmp(narrow, "A") == 0);
}

static void test_the_flag_chooses_which_name_the_line_carries(void)
{
    // Set: the list is authoritative, whatever the stream calls itself.
    assert(strcmp(ui_now_playing_station_name(true, "Радио Шоколад", "CHOCO FM 128"),
                  "Радио Шоколад") == 0);
    // Clear: the stream's own name wins.
    assert(strcmp(ui_now_playing_station_name(false, "Радио Шоколад", "CHOCO FM 128"),
                  "CHOCO FM 128") == 0);
}

static void test_a_silent_stream_still_names_the_station(void)
{
    /* Plenty of stations send no icy-name at all. Honouring the flag literally
     * there would leave the line blank, which reads as a fault rather than as
     * a station that simply says nothing. */
    assert(strcmp(ui_now_playing_station_name(false, "Радио Шоколад", ""), "Радио Шоколад") == 0);
    assert(strcmp(ui_now_playing_station_name(false, "Радио Шоколад", NULL), "Радио Шоколад") == 0);
    // Neither name known - empty, never NULL: the caller hands this to a label.
    assert(strcmp(ui_now_playing_station_name(false, NULL, NULL), "") == 0);
}

static void test_a_station_reads_as_a_name_a_performer_and_a_track(void)
{
    ui_now_playing_t now;
    ui_now_playing_for_station(true, "Радио Шоколад", "CHOCO FM 128",
                               "Sam Feldt - Be My Lover", &now);
    assert(strcmp(now.heading, "Радио Шоколад") == 0);
    assert(strcmp(now.artist, "Sam Feldt") == 0);
    assert(strcmp(now.title, "Be My Lover") == 0);

    /* Silence from the stream leaves the track empty rather than repeating the
     * station: the line says nothing because nothing was said. */
    ui_now_playing_for_station(true, "Радио Шоколад", "", "", &now);
    assert(strcmp(now.heading, "Радио Шоколад") == 0);
    assert(now.artist[0] == '\0');
    assert(now.title[0] == '\0');
}

static void test_a_file_reads_out_of_its_tags(void)
{
    audio_tags_t tags;
    memset(&tags, 0, sizeof(tags));
    snprintf(tags.title, sizeof(tags.title), "%s", "Ständchen auf dem Regenbogen");
    snprintf(tags.artist, sizeof(tags.artist), "%s", "Will Glahe & Sein Orchester");
    snprintf(tags.album, sizeof(tags.album), "%s", "Eskapaden-Foxtrott");

    ui_now_playing_t now;
    ui_now_playing_for_file("/usb0/Music/Will Glahe", "1---staendchen.mp3", &tags, &now);
    assert(strcmp(now.heading, "Eskapaden-Foxtrott") == 0);
    assert(strcmp(now.artist, "Will Glahe & Sein Orchester") == 0);
    assert(strcmp(now.title, "Ständchen auf dem Regenbogen") == 0);
}

static void test_each_line_of_a_file_falls_back_on_its_own(void)
{
    /* A file can name its album and not its performer, and half a set of tags
     * is still better than none. */
    audio_tags_t tags;
    memset(&tags, 0, sizeof(tags));
    snprintf(tags.album, sizeof(tags.album), "%s", "Eskapaden-Foxtrott");

    ui_now_playing_t now;
    ui_now_playing_for_file("/usb0/Music", "16---eskapaden.mp3", &tags, &now);
    assert(strcmp(now.heading, "Eskapaden-Foxtrott") == 0);
    assert(now.artist[0] == '\0');
    // No tag title: the name on the drive is what identifies the track.
    assert(strcmp(now.title, "16---eskapaden.mp3") == 0);

    /* No tags at all: the directory stands in for the album, because a folder
     * is what the listener was looking at when they chose the file. */
    ui_now_playing_for_file("/usb0/Music", "16---eskapaden.mp3", NULL, &now);
    assert(strcmp(now.heading, "/usb0/Music") == 0);
    assert(now.artist[0] == '\0');
    assert(strcmp(now.title, "16---eskapaden.mp3") == 0);
}

static void test_neither_builder_writes_through_a_null(void)
{
    ui_now_playing_for_file(NULL, NULL, NULL, NULL);
    ui_now_playing_for_station(false, NULL, NULL, NULL, NULL);
    ui_now_playing_t now;
    ui_now_playing_for_file(NULL, NULL, NULL, &now);
    assert(now.heading[0] == '\0' && now.artist[0] == '\0' && now.title[0] == '\0');
}

int main(void)
{
    test_an_icy_title_splits_into_performer_and_track();
    test_a_spaced_dash_of_either_kind_counts();
    test_a_lopsided_or_missing_title_is_left_whole();
    test_a_performer_that_does_not_fit_is_not_truncated();
    test_a_title_that_only_repeats_the_line_above_is_not_split();
    test_what_a_station_sent_is_made_renderable();
    test_the_flag_chooses_which_name_the_line_carries();
    test_a_silent_stream_still_names_the_station();
    test_a_station_reads_as_a_name_a_performer_and_a_track();
    test_a_file_reads_out_of_its_tags();
    test_each_line_of_a_file_falls_back_on_its_own();
    test_neither_builder_writes_through_a_null();
    puts("ui_now_playing tests passed");
    return 0;
}
