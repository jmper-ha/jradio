#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "usb_track_progress.h"

static void test_elapsed_counts_what_was_played_not_what_was_read(void)
{
    /* One second of CD-quality stereo is 176400 bytes. Counting decoded PCM is
     * the whole point: the reader runs a full buffer ahead of the speaker. */
    assert(usb_track_elapsed_seconds(176400U, 44100U, 2U, 16U) == 1U);
    assert(usb_track_elapsed_seconds(176400U * 161U, 44100U, 2U, 16U) == 161U);
    /* Part of a second rounds down - a clock that reads ahead of the audio is
     * worse than one a fraction behind. */
    assert(usb_track_elapsed_seconds(176399U, 44100U, 2U, 16U) == 0U);
}

static void test_elapsed_follows_the_format_not_a_fixed_rate(void)
{
    /* Mono at 22050 runs four times as long per byte as CD stereo. */
    assert(usb_track_elapsed_seconds(44100U, 22050U, 1U, 16U) == 1U);
    /* 24-bit stereo at 48 kHz. */
    assert(usb_track_elapsed_seconds(288000U, 48000U, 2U, 24U) == 1U);
}

static void test_elapsed_is_zero_until_the_format_is_known(void)
{
    /* Before the first frame is decoded there is no rate to divide by, and
     * guessing one would put a wrong time on the screen. */
    assert(usb_track_elapsed_seconds(1000000U, 0U, 2U, 16U) == 0U);
    assert(usb_track_elapsed_seconds(1000000U, 44100U, 0U, 16U) == 0U);
    assert(usb_track_elapsed_seconds(1000000U, 44100U, 2U, 0U) == 0U);
    assert(usb_track_elapsed_seconds(0U, 44100U, 2U, 16U) == 0U);
}

static void test_total_comes_from_size_and_bitrate(void)
{
    /* A 5 MB file at 128 kbps is 320 seconds - the arithmetic every player
     * does when a format does not state its own duration. */
    assert(usb_track_total_seconds(5120000U, 0U, 128U) == 320U);
    /* FLAC at 1058 kbps. */
    assert(usb_track_total_seconds(52900000U, 0U, 1058U) == 400U);
}

static void test_the_header_is_not_counted_as_audio(void)
{
    /* A WAV header or an ID3 tag is not playing time. On a short file the
     * difference is the whole point: 44 bytes of header on a 44144-byte file
     * is a quarter of a second of the total. */
    const uint64_t size = 176400U + 44U;
    assert(usb_track_total_seconds(size, 44U, 1411U) == usb_track_total_seconds(176400U, 0U, 1411U));
    /* A file that is nothing but header has no duration. */
    assert(usb_track_total_seconds(44U, 44U, 1411U) == 0U);
    assert(usb_track_total_seconds(10U, 44U, 1411U) == 0U);
}

static void test_a_flac_gets_its_length_from_its_sample_count(void)
{
    /* The case that had no bar at all. FLAC frames are as large as the music
     * needs, so size and bitrate say nothing; STREAMINFO's sample count is
     * exact. The album this was built for: 8:19 of 44.1 kHz. */
    assert(usb_track_sampled_seconds(22050000U, 44100U) == 500U);
    assert(usb_track_sampled_seconds(21987648U, 44100U) == 498U);
    // 96 kHz is the same arithmetic and must not be assumed away.
    assert(usb_track_sampled_seconds(9600000U, 96000U) == 100U);
}

static void test_a_stream_written_to_a_file_has_no_length(void)
{
    // What an encoder writes when it was fed a pipe: it never knew the total.
    assert(usb_track_sampled_seconds(0U, 44100U) == 0U);
    assert(usb_track_sampled_seconds(22050000U, 0U) == 0U);
}

static void test_a_format_with_no_stated_rate_gets_an_average(void)
{
    /* Needed for the jump arithmetic and the codec line, and only ever an
     * average - which for a 500-second file of 62.5 MB is 1000 kbps. */
    assert(usb_track_average_bitrate_kbps(62500000U, 0U, 500U) == 1000U);
    // The header is not audio, here as everywhere else.
    assert(usb_track_average_bitrate_kbps(62500000U + 8000U, 8000U, 500U) == 1000U);
    // And it has to round-trip with the seek: an average is what turns
    // seconds back into an offset.
    assert(usb_track_seek_offset(62500000U, 0U, 1000U, 250U) == 31250000U);
}

static void test_an_average_of_nothing_is_not_reported(void)
{
    assert(usb_track_average_bitrate_kbps(62500000U, 0U, 0U) == 0U);
    assert(usb_track_average_bitrate_kbps(100U, 200U, 10U) == 0U);
    // Clamped to the field rather than wrapped inside it.
    assert(usb_track_average_bitrate_kbps(UINT64_C(1) << 40, 0U, 1U) == UINT16_MAX);
}

static void test_an_unknown_bitrate_gives_no_duration(void)
{
    /* Normal for the first moments of a track. Returning 0 lets the caller
     * hide the bar rather than draw an empty one that means nothing. */
    assert(usb_track_total_seconds(5120000U, 0U, 0U) == 0U);
}

static void test_the_bar_is_hidden_when_there_is_nothing_to_show(void)
{
    uint8_t percent = 42U;
    assert(!usb_track_progress_percent(10U, 0U, &percent));
    assert(!usb_track_progress_percent(10U, 100U, NULL));
}

static void test_the_bar_tracks_the_position(void)
{
    uint8_t percent = 0U;
    assert(usb_track_progress_percent(0U, 400U, &percent));
    assert(percent == 0U);
    assert(usb_track_progress_percent(100U, 400U, &percent));
    assert(percent == 25U);
    assert(usb_track_progress_percent(400U, 400U, &percent));
    assert(percent == 100U);
}

static void test_the_bar_cannot_overshoot_its_track(void)
{
    /* A variable-bitrate file routinely outlives the estimate. Pinned at the
     * end reads as "nearly over"; past the end reads as broken. */
    uint8_t percent = 0U;
    assert(usb_track_progress_percent(900U, 400U, &percent));
    assert(percent == 100U);
}

static void test_times_are_written_the_way_players_write_them(void)
{
    char text[16];
    usb_track_time_text(text, sizeof(text), 161U);
    assert(strcmp(text, "2:41") == 0);
    /* Seconds always padded, minutes not - "2:05", never "2:5" or "02:05". */
    usb_track_time_text(text, sizeof(text), 125U);
    assert(strcmp(text, "2:05") == 0);
    usb_track_time_text(text, sizeof(text), 0U);
    assert(strcmp(text, "0:00") == 0);
    usb_track_time_text(text, sizeof(text), 59U);
    assert(strcmp(text, "0:59") == 0);
}

static void test_a_long_track_grows_an_hours_field(void)
{
    /* A DJ set or an audiobook chapter. Without this, 1:02:03 would read as
     * 62:03 and take a moment to parse. */
    char text[16];
    usb_track_time_text(text, sizeof(text), 3723U);
    assert(strcmp(text, "1:02:03") == 0);
    usb_track_time_text(text, sizeof(text), 3600U);
    assert(strcmp(text, "1:00:00") == 0);
    /* Just short of an hour keeps the short form. */
    usb_track_time_text(text, sizeof(text), 3599U);
    assert(strcmp(text, "59:59") == 0);

    usb_track_time_text(NULL, sizeof(text), 10U);
    usb_track_time_text(text, 0U, 10U);
}

static void test_a_seek_lands_where_the_length_estimate_says_it_should(void)
{
    /* The inverse of the total: 128 kbps is 16000 bytes a second, so a minute
     * in is 960000 bytes past the start of the audio. */
    assert(usb_track_seek_offset(4000000U, 0U, 128U, 60U) == 960000U);
    /* The header is not audio, so it is added on rather than seeked into. */
    assert(usb_track_seek_offset(4000000U, 2048U, 128U, 60U) == 960000U + 2048U);
    /* The start of the track is the start of the audio, whatever precedes it. */
    assert(usb_track_seek_offset(4000000U, 2048U, 128U, 0U) == 2048U);
}

static void test_a_seek_never_leaves_the_file(void)
{
    /* Past the end lands on the end: the track then finishes, which is at
     * least a place the player knows what to do with. */
    assert(usb_track_seek_offset(500000U, 0U, 128U, 600U) == 500000U);
    /* Before the bitrate is known there is no way to convert a time to an
     * offset, so the only safe answer is where the audio starts. */
    assert(usb_track_seek_offset(4000000U, 2048U, 0U, 60U) == 2048U);
    /* A file smaller than its own header is corrupt; do not seek into it. */
    assert(usb_track_seek_offset(1024U, 2048U, 128U, 60U) == 2048U);
}

static void test_the_position_carries_on_from_where_the_seek_landed(void)
{
    /* The counter the elapsed reading is derived from has to be moved with the
     * file, or the display would count up from zero again after a jump. Round
     * trip through the reading that reads it: 16-bit stereo at 44.1 kHz. */
    const uint64_t bytes = usb_track_pcm_bytes(90U, 44100U, 2U, 16U);
    assert(bytes == 90ULL * 44100U * 4U);
    assert(usb_track_elapsed_seconds(bytes, 44100U, 2U, 16U) == 90U);

    /* Nothing known about the format yet means no byte count to aim for. */
    assert(usb_track_pcm_bytes(90U, 0U, 2U, 16U) == 0U);
    assert(usb_track_pcm_bytes(90U, 44100U, 0U, 16U) == 0U);
    assert(usb_track_pcm_bytes(90U, 44100U, 2U, 4U) == 0U);
}

int main(void)
{
    test_elapsed_counts_what_was_played_not_what_was_read();
    test_elapsed_follows_the_format_not_a_fixed_rate();
    test_elapsed_is_zero_until_the_format_is_known();
    test_total_comes_from_size_and_bitrate();
    test_the_header_is_not_counted_as_audio();
    test_a_flac_gets_its_length_from_its_sample_count();
    test_a_stream_written_to_a_file_has_no_length();
    test_a_format_with_no_stated_rate_gets_an_average();
    test_an_average_of_nothing_is_not_reported();
    test_an_unknown_bitrate_gives_no_duration();
    test_the_bar_is_hidden_when_there_is_nothing_to_show();
    test_the_bar_tracks_the_position();
    test_the_bar_cannot_overshoot_its_track();
    test_times_are_written_the_way_players_write_them();
    test_a_long_track_grows_an_hours_field();
    test_a_seek_lands_where_the_length_estimate_says_it_should();
    test_a_seek_never_leaves_the_file();
    test_the_position_carries_on_from_where_the_seek_landed();
    puts("usb_track_progress tests passed");
    return 0;
}
