#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "hls_playlist.h"

/* The two playlists Relax FM actually serves, kept verbatim so a change in the
 * parser is checked against real input rather than against what the parser
 * happens to accept. */
static const char k_master[] =
    "#EXTM3U\n"
    "#EXT-X-INDEPENDENT-SEGMENTS\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=129945,AVERAGE-BANDWIDTH=128190,CODECS=\"mp4a.40.2\"\n"
    "playlist.m3u8?hlssid=48dc584ddf3c4068954f380038d4454d\n";

static const char k_media[] =
    "#EXTM3U\n"
    "#EXT-X-VERSION:3\n"
    "#EXT-X-TARGETDURATION:6\n"
    "#EXT-X-MEDIA-SEQUENCE:2\n"
    "#EXT-X-DISCONTINUITY-SEQUENCE:0\n"
    "#EXT-X-START:TIME-OFFSET=0\n"
    "#EXT-X-PROGRAM-DATE-TIME:2026-08-17T17:51:11.643Z\n"
    "#EXT-X-DISCONTINUITY\n"
    "#EXTINF:5.990748,\n"
    "l0_6a834a545ce8a2c4b30a06d2.aac\n"
    "#EXTINF:5.990748,\n"
    "l0_6a834a5a5ce8a2c4b30a06d3.aac\n"
    "#EXTINF:5.990748,\n"
    "l0_6a834a605ce8a2c4b30a06d4.aac\n"
    "#EXTINF:5.990748,\n"
    "l0_6a834a665ce8a2c4b30a06d5.aac\n";

static void test_a_playlist_url_is_told_from_a_stream_url(void)
{
    assert(hls_url_is_playlist("https://host/relaxfm/128/playlist.m3u8"));
    assert(hls_url_is_playlist("https://host/PLAYLIST.M3U8"));
    /* The session token lives in the query string, so the extension is not
     * last in the URL. */
    assert(hls_url_is_playlist("https://host/playlist.m3u8?hlssid=48dc584d"));
    assert(hls_url_is_playlist("https://host/playlist.m3u8#start"));

    assert(!hls_url_is_playlist("http://choco.hostingradio.ru:10010/fm"));
    assert(!hls_url_is_playlist("http://host/stream.aac"));
    /* Named in a parameter but not the document: still not a playlist. */
    assert(!hls_url_is_playlist("http://host/stream.aac?next=playlist.m3u8"));
    assert(!hls_url_is_playlist(NULL));
    assert(!hls_url_is_playlist("m3u8"));
}

static void test_a_master_playlist_yields_a_variant_not_segments(void)
{
    hls_playlist_t playlist;
    assert(hls_playlist_parse(k_master, sizeof(k_master) - 1U, &playlist));
    assert(playlist.is_master);
    assert(playlist.segment_count == 0U);
    assert(playlist.variant_bandwidth == 129945U);
    /* AVERAGE-BANDWIDTH must not be mistaken for BANDWIDTH. */
    assert(playlist.variant_bandwidth != 128190U);
    assert(strcmp(playlist.variant_uri,
                  "playlist.m3u8?hlssid=48dc584ddf3c4068954f380038d4454d") == 0);
}

static void test_the_cheapest_variant_wins(void)
{
    /* One core decodes while the UI runs, and the input buffer is fixed in
     * bytes; the lowest bitrate is the one most likely to play through. */
    static const char text[] =
        "#EXTM3U\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=320000\n"
        "high.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=64000\n"
        "low.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=128000\n"
        "mid.m3u8\n";
    hls_playlist_t playlist;
    assert(hls_playlist_parse(text, sizeof(text) - 1U, &playlist));
    assert(playlist.is_master);
    assert(strcmp(playlist.variant_uri, "low.m3u8") == 0);
    assert(playlist.variant_bandwidth == 64000U);
}

static void test_a_media_playlist_yields_segments_with_durations(void)
{
    hls_playlist_t playlist;
    assert(hls_playlist_parse(k_media, sizeof(k_media) - 1U, &playlist));
    assert(!playlist.is_master);
    assert(playlist.segment_count == 4U);
    assert(playlist.media_sequence == 2U);
    assert(playlist.target_duration_ms == 6000U);
    assert(!playlist.has_endlist);
    assert(strcmp(playlist.segments[0].uri, "l0_6a834a545ce8a2c4b30a06d2.aac") == 0);
    assert(strcmp(playlist.segments[3].uri, "l0_6a834a665ce8a2c4b30a06d5.aac") == 0);
    /* 5.990748 s truncated to milliseconds. */
    assert(playlist.segments[0].duration_ms == 5990U);
}

static void test_crlf_and_stray_whitespace_are_tolerated(void)
{
    /* Trailing whitespace on a URI line would go straight into the request
     * path, so it has to come off here. */
    static const char text[] =
        "#EXTM3U\r\n"
        "#EXT-X-TARGETDURATION:10\r\n"
        "#EXTINF:9.0,\r\n"
        "  seg1.aac  \r\n"
        "\r\n"
        "#EXTINF:9.0,\r\n"
        "seg2.aac\r\n";
    hls_playlist_t playlist;
    assert(hls_playlist_parse(text, sizeof(text) - 1U, &playlist));
    assert(playlist.segment_count == 2U);
    assert(strcmp(playlist.segments[0].uri, "seg1.aac") == 0);
    assert(strcmp(playlist.segments[1].uri, "seg2.aac") == 0);
    assert(playlist.segments[0].duration_ms == 9000U);
}

static void test_input_that_is_not_a_playlist_is_rejected(void)
{
    /* A station whose URL merely ends in .m3u8 but serves audio, or an error
     * page: both must fail here rather than be parsed into nothing. */
    static const char html[] = "<html><body>404</body></html>";
    hls_playlist_t playlist;
    assert(!hls_playlist_parse(html, sizeof(html) - 1U, &playlist));
    assert(!hls_playlist_parse("", 0U, &playlist));
    assert(!hls_playlist_parse(NULL, 10U, &playlist));
    assert(!hls_playlist_parse(k_media, sizeof(k_media) - 1U, NULL));
}

static void test_an_empty_live_playlist_is_not_an_error(void)
{
    /* Between refreshes a live playlist can arrive with nothing new. That is a
     * wait, not a failure, and the caller distinguishes them by the return. */
    static const char text[] = "#EXTM3U\n#EXT-X-TARGETDURATION:6\n";
    hls_playlist_t playlist;
    assert(hls_playlist_parse(text, sizeof(text) - 1U, &playlist));
    assert(playlist.segment_count == 0U);
    assert(!playlist.is_master);
}

static void test_a_window_longer_than_the_buffer_keeps_the_live_edge(void)
{
    /* Dropping the newest segments would strand playback behind the window and
     * it would never catch up, so the oldest have to go instead - and the
     * sequence number has to follow them. */
    char text[8192];
    int used = snprintf(text, sizeof(text), "#EXTM3U\n#EXT-X-MEDIA-SEQUENCE:100\n");
    for (unsigned int index = 0U; index < HLS_SEGMENT_MAX + 5U; ++index) {
        used += snprintf(text + used, sizeof(text) - (size_t)used,
                         "#EXTINF:6.0,\nseg%u.aac\n", index);
    }
    hls_playlist_t playlist;
    assert(hls_playlist_parse(text, (size_t)used, &playlist));
    assert(playlist.segment_count == HLS_SEGMENT_MAX);
    assert(playlist.media_sequence == 105U);
    char expected[32];
    snprintf(expected, sizeof(expected), "seg%u.aac", HLS_SEGMENT_MAX + 4U);
    assert(strcmp(playlist.segments[HLS_SEGMENT_MAX - 1U].uri, expected) == 0);
    assert(strcmp(playlist.segments[0].uri, "seg5.aac") == 0);
}

static void test_an_overlong_uri_is_dropped_not_truncated(void)
{
    /* A truncated URI still fetches something, and the wrong file is worse
     * than a gap. */
    char text[HLS_URI_MAX * 2U];
    int used = snprintf(text, sizeof(text), "#EXTM3U\n#EXTINF:6.0,\n");
    for (unsigned int index = 0U; index < HLS_URI_MAX + 10U; ++index) {
        text[used++] = 'a';
    }
    used += snprintf(text + used, sizeof(text) - (size_t)used, "\n#EXTINF:6.0,\nshort.aac\n");
    hls_playlist_t playlist;
    assert(hls_playlist_parse(text, (size_t)used, &playlist));
    assert(playlist.segment_count == 1U);
    assert(strcmp(playlist.segments[0].uri, "short.aac") == 0);
}

static void test_relative_references_resolve_against_their_document(void)
{
    char out[256];
    const char *base = "https://hls-01-gpm.hostingradio.ru/relaxfm36560/128/playlist.m3u8";

    /* A sibling name: replace the last path element. */
    assert(hls_url_resolve(base, "l0_6a834a54.aac", out, sizeof(out)));
    assert(strcmp(out, "https://hls-01-gpm.hostingradio.ru/relaxfm36560/128/l0_6a834a54.aac") == 0);

    /* An absolute path: keep only scheme and host. */
    assert(hls_url_resolve(base, "/other/seg.aac", out, sizeof(out)));
    assert(strcmp(out, "https://hls-01-gpm.hostingradio.ru/other/seg.aac") == 0);

    /* An absolute URL: pass through, even to a different host. */
    assert(hls_url_resolve(base, "http://cdn.example.com/a/b.aac", out, sizeof(out)));
    assert(strcmp(out, "http://cdn.example.com/a/b.aac") == 0);
}

static void test_the_documents_query_string_is_not_inherited(void)
{
    /* The variant playlist carries a session token; segments named beside it
     * must not pick that token up, and must not lose the directory either. */
    char out[256];
    const char *base = "https://host/relaxfm/128/playlist.m3u8?hlssid=48dc584d";
    assert(hls_url_resolve(base, "l0_6a834a54.aac", out, sizeof(out)));
    assert(strcmp(out, "https://host/relaxfm/128/l0_6a834a54.aac") == 0);
}

static void test_resolution_fails_rather_than_producing_a_short_url(void)
{
    char out[32];
    const char *base = "https://hls-01-gpm.hostingradio.ru/relaxfm36560/128/playlist.m3u8";
    assert(!hls_url_resolve(base, "some_rather_long_segment_name.aac", out, sizeof(out)));
    assert(!hls_url_resolve(base, "/some_rather_long_absolute_path.aac", out, sizeof(out)));
    assert(!hls_url_resolve("http://host/very_long_name_here.m3u8",
                            "http://host/another_very_long_name.aac", out, sizeof(out)));
    assert(!hls_url_resolve(base, "", out, sizeof(out)));
    assert(!hls_url_resolve(base, "seg.aac", NULL, sizeof(out)));
    assert(!hls_url_resolve(NULL, "seg.aac", out, sizeof(out)));
    /* Not a URL at all. */
    assert(!hls_url_resolve("not-a-url", "seg.aac", out, sizeof(out)));
}

static void test_a_host_only_base_still_resolves(void)
{
    char out[64];
    assert(hls_url_resolve("https://host", "seg.aac", out, sizeof(out)));
    assert(strcmp(out, "https://host/seg.aac") == 0);
}

static void test_live_playback_starts_back_from_the_edge(void)
{
    hls_playlist_t playlist;
    assert(hls_playlist_parse(k_media, sizeof(k_media) - 1U, &playlist));
    /* Four segments, sequence 2..5: three back from the last one is 2. */
    assert(hls_playlist_live_start(&playlist) == 2U);

    /* With a full window the start is exactly three from the end, leaving ~18 s
     * of published audio ahead - enough that a slow refresh does not run the
     * decoder dry, close enough that the stream is still live. */
    char text[4096];
    int used = snprintf(text, sizeof(text), "#EXTM3U\n#EXT-X-MEDIA-SEQUENCE:100\n");
    for (unsigned int index = 0U; index < 10U; ++index) {
        used += snprintf(text + used, sizeof(text) - (size_t)used,
                         "#EXTINF:6.0,\nseg%u.aac\n", index);
    }
    assert(hls_playlist_parse(text, (size_t)used, &playlist));
    assert(hls_playlist_live_start(&playlist) == 106U);
}

static void test_a_finished_recording_starts_at_the_beginning(void)
{
    /* ENDLIST means the whole thing is there; joining near the end would skip
     * most of it. */
    static const char text[] =
        "#EXTM3U\n"
        "#EXT-X-MEDIA-SEQUENCE:0\n"
        "#EXTINF:6.0,\nseg0.aac\n"
        "#EXTINF:6.0,\nseg1.aac\n"
        "#EXTINF:6.0,\nseg2.aac\n"
        "#EXTINF:6.0,\nseg3.aac\n"
        "#EXTINF:6.0,\nseg4.aac\n"
        "#EXT-X-ENDLIST\n";
    hls_playlist_t playlist;
    assert(hls_playlist_parse(text, sizeof(text) - 1U, &playlist));
    assert(playlist.has_endlist);
    assert(hls_playlist_live_start(&playlist) == 0U);
}

static void test_a_short_playlist_starts_at_its_first_segment(void)
{
    static const char text[] =
        "#EXTM3U\n#EXT-X-MEDIA-SEQUENCE:7\n#EXTINF:6.0,\nseg0.aac\n";
    hls_playlist_t playlist;
    assert(hls_playlist_parse(text, sizeof(text) - 1U, &playlist));
    assert(hls_playlist_live_start(&playlist) == 7U);
    assert(hls_playlist_live_start(NULL) == 0U);
}

static void test_a_sequence_number_finds_its_segment_across_a_slide(void)
{
    hls_playlist_t playlist;
    assert(hls_playlist_parse(k_media, sizeof(k_media) - 1U, &playlist));
    assert(hls_playlist_index_of(&playlist, 2U) == 0U);
    assert(hls_playlist_index_of(&playlist, 5U) == 3U);

    /* Not published yet: wait for the next refresh. */
    assert(hls_playlist_index_of(&playlist, 6U) == HLS_SEGMENT_NONE);
    /* Already slid out of the window: the caller has fallen behind and must
     * rejoin, which is why this is not the same answer as "keep waiting" to
     * anyone who checks the sequence too. */
    assert(hls_playlist_index_of(&playlist, 1U) == HLS_SEGMENT_NONE);
    assert(hls_playlist_index_of(NULL, 2U) == HLS_SEGMENT_NONE);
}

static void test_the_id3_tag_ahead_of_the_audio_is_measured(void)
{
    /* Exactly what Relax FM prepends: ID3v2.4, no flags, 63 bytes of a PRIV
     * frame holding the Apple transport timestamp, then the first ADTS frame.
     * The decoder looks for a sync word, so the tag has to be dropped. */
    uint8_t segment[128] = {0};
    memcpy(segment, "ID3", 3U);
    segment[3] = 0x04U;
    segment[4] = 0x00U;
    segment[5] = 0x00U;
    segment[9] = 63U;
    assert(hls_id3_prefix_length(segment, sizeof(segment)) == 73U);

    /* The footer flag adds ten more bytes. */
    segment[5] = 0x10U;
    assert(hls_id3_prefix_length(segment, sizeof(segment)) == 83U);
}

static void test_a_syncsafe_size_spans_all_four_bytes(void)
{
    uint8_t segment[16] = {0};
    memcpy(segment, "ID3", 3U);
    segment[3] = 0x03U;
    segment[6] = 0x01U;
    segment[7] = 0x02U;
    segment[8] = 0x03U;
    segment[9] = 0x04U;
    assert(hls_id3_prefix_length(segment, sizeof(segment)) ==
           10U + ((1U << 21U) | (2U << 14U) | (3U << 7U) | 4U));
}

static void test_audio_is_not_mistaken_for_a_tag(void)
{
    /* A segment that starts straight on an ADTS sync word must be passed
     * through whole; skipping ten bytes of it would break the first frame. */
    const uint8_t adts[] = {0xffU, 0xf1U, 0x50U, 0x80U, 0x39U, 0x21U, 0x28U,
                            0x00U, 0x00U, 0x00U, 0x00U, 0x00U};
    assert(hls_id3_prefix_length(adts, sizeof(adts)) == 0U);

    /* "ID3" with a high bit set in the size is not a syncsafe integer, so it
     * is not a tag either. */
    uint8_t fake[16] = {0};
    memcpy(fake, "ID3", 3U);
    fake[3] = 0x04U;
    fake[8] = 0x80U;
    assert(hls_id3_prefix_length(fake, sizeof(fake)) == 0U);

    /* A reserved version byte. */
    uint8_t reserved[16] = {0};
    memcpy(reserved, "ID3", 3U);
    reserved[3] = 0xffU;
    assert(hls_id3_prefix_length(reserved, sizeof(reserved)) == 0U);

    assert(hls_id3_prefix_length(NULL, 16U) == 0U);
    /* Too short to decide: the caller must present the first ten bytes. */
    assert(hls_id3_prefix_length((const uint8_t *)"ID3", 3U) == 0U);
}

int main(void)
{
    test_a_playlist_url_is_told_from_a_stream_url();
    test_a_master_playlist_yields_a_variant_not_segments();
    test_the_cheapest_variant_wins();
    test_a_media_playlist_yields_segments_with_durations();
    test_crlf_and_stray_whitespace_are_tolerated();
    test_input_that_is_not_a_playlist_is_rejected();
    test_an_empty_live_playlist_is_not_an_error();
    test_a_window_longer_than_the_buffer_keeps_the_live_edge();
    test_an_overlong_uri_is_dropped_not_truncated();
    test_relative_references_resolve_against_their_document();
    test_the_documents_query_string_is_not_inherited();
    test_resolution_fails_rather_than_producing_a_short_url();
    test_a_host_only_base_still_resolves();
    test_live_playback_starts_back_from_the_edge();
    test_a_finished_recording_starts_at_the_beginning();
    test_a_short_playlist_starts_at_its_first_segment();
    test_a_sequence_number_finds_its_segment_across_a_slide();
    test_the_id3_tag_ahead_of_the_audio_is_measured();
    test_a_syncsafe_size_spans_all_four_bytes();
    test_audio_is_not_mistaken_for_a_tag();
    puts("hls_playlist tests passed");
    return 0;
}
