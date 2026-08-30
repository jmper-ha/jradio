#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "radio_stream_format.h"
#include "station_catalog.h"

static void test_aac_url_selects_aac_decoder(void)
{
    const radio_stream_format_t format = radio_stream_format_from_url(
        "http://online-1.gkvr.ru:8000/europa_krd_128.aac");

    assert(format == RADIO_STREAM_FORMAT_AAC);
    assert(strcmp(radio_stream_format_raw_uri(format), "raw://radio/stream.aac") == 0);
    assert(strcmp(radio_stream_format_codec_name(format), "AAC") == 0);
}

static void test_mp3_and_unknown_urls_keep_mp3_decoder(void)
{
    assert(radio_stream_format_from_url("http://example.invalid/stream.mp3") ==
           RADIO_STREAM_FORMAT_MP3);
    assert(radio_stream_format_from_url("http://example.invalid/stream") ==
           RADIO_STREAM_FORMAT_MP3);
}

static void test_ogg_flac_url_selects_ogg_container_decoder(void)
{
    const radio_stream_format_t format = radio_stream_format_from_url(
        "http://stream.radioparadise.com/eclectic-flac");

    assert(strcmp(radio_stream_format_codec_name(format), "FLAC") == 0);
    assert(strcmp(radio_stream_format_raw_uri(format), "raw://radio/stream.ogg") == 0);

    const radio_stream_format_t native_flac = radio_stream_format_from_url(
        "http://example.invalid/stream.flac");
    assert(native_flac == RADIO_STREAM_FORMAT_FLAC);
    assert(strcmp(radio_stream_format_raw_uri(native_flac), "raw://radio/stream.flac") == 0);
}

/* Builds one Ogg page around `body`, with `segments` entries in the segment
 * table - the table is what stands between the 27-byte header and the body, so
 * a reader that assumes a fixed offset lands inside it. */
static size_t make_ogg_page(uint8_t *page, size_t capacity, unsigned int segments,
                            const char *body, size_t body_length)
{
    const size_t header = 27U;
    const size_t total = header + segments + body_length;
    assert(total <= capacity);
    memset(page, 0, total);
    memcpy(page, "OggS", 4U);
    page[26] = (uint8_t)segments;
    memcpy(page + header + segments, body, body_length);
    return total;
}

static void test_the_codec_inside_an_ogg_stream_is_read_from_its_first_page(void)
{
    /* Every one of these arrives as "audio/ogg", so the container cannot say
     * which codec is playing and the first page has to. */
    uint8_t page[64];
    size_t length;

    length = make_ogg_page(page, sizeof(page), 1U, "\x7f" "FLAC", 5U);
    assert(radio_stream_format_from_ogg_page(page, length, RADIO_STREAM_FORMAT_MP3) ==
           RADIO_STREAM_FORMAT_OGG_FLAC);

    length = make_ogg_page(page, sizeof(page), 1U, "\x01vorbis", 7U);
    assert(radio_stream_format_from_ogg_page(page, length, RADIO_STREAM_FORMAT_MP3) ==
           RADIO_STREAM_FORMAT_OGG_VORBIS);

    length = make_ogg_page(page, sizeof(page), 1U, "OpusHead", 8U);
    assert(radio_stream_format_from_ogg_page(page, length, RADIO_STREAM_FORMAT_MP3) ==
           RADIO_STREAM_FORMAT_OGG_OPUS);

    /* A longer segment table pushes the body further out; the signature has to
     * be looked for after the table, not at a fixed offset. */
    length = make_ogg_page(page, sizeof(page), 6U, "OpusHead", 8U);
    assert(radio_stream_format_from_ogg_page(page, length, RADIO_STREAM_FORMAT_MP3) ==
           RADIO_STREAM_FORMAT_OGG_OPUS);
}

static void test_an_unreadable_ogg_page_keeps_the_earlier_guess(void)
{
    /* The fallback is what the Content-Type said. Anything this cannot read -
     * a buffer that does not start on a page boundary, a truncated page, a
     * codec this build has no decoder for - leaves that answer alone rather
     * than renaming the stream to something wrong. */
    uint8_t page[64];
    const radio_stream_format_t guess = RADIO_STREAM_FORMAT_OGG_FLAC;

    size_t length = make_ogg_page(page, sizeof(page), 1U, "Speex   ", 8U);
    assert(radio_stream_format_from_ogg_page(page, length, guess) == guess);

    length = make_ogg_page(page, sizeof(page), 1U, "OpusHead", 8U);
    /* Cut short: the header is there, the body is not. */
    assert(radio_stream_format_from_ogg_page(page, 28U, guess) == guess);
    assert(radio_stream_format_from_ogg_page(page, 4U, guess) == guess);
    assert(radio_stream_format_from_ogg_page(NULL, length, guess) == guess);

    /* Mid-stream bytes, not a page start. */
    memcpy(page, "fLaC", 4U);
    assert(radio_stream_format_from_ogg_page(page, length, guess) == guess);
}

static void test_every_ogg_codec_names_itself_and_shares_one_decoder(void)
{
    assert(radio_stream_format_is_ogg(RADIO_STREAM_FORMAT_OGG_FLAC));
    assert(radio_stream_format_is_ogg(RADIO_STREAM_FORMAT_OGG_VORBIS));
    assert(radio_stream_format_is_ogg(RADIO_STREAM_FORMAT_OGG_OPUS));
    assert(!radio_stream_format_is_ogg(RADIO_STREAM_FORMAT_FLAC));
    assert(!radio_stream_format_is_ogg(RADIO_STREAM_FORMAT_MP3));

    assert(strcmp(radio_stream_format_codec_name(RADIO_STREAM_FORMAT_OGG_VORBIS), "Vorbis") == 0);
    assert(strcmp(radio_stream_format_codec_name(RADIO_STREAM_FORMAT_OGG_OPUS), "Opus") == 0);
    assert(strcmp(radio_stream_format_codec_name(RADIO_STREAM_FORMAT_OGG_FLAC), "FLAC") == 0);

    /* One container, so one raw URI whatever is inside it. */
    assert(strcmp(radio_stream_format_raw_uri(RADIO_STREAM_FORMAT_OGG_VORBIS),
                  "raw://radio/stream.ogg") == 0);
    assert(strcmp(radio_stream_format_raw_uri(RADIO_STREAM_FORMAT_OGG_OPUS),
                  "raw://radio/stream.ogg") == 0);
}

static void test_icy_bitrate_header_is_parsed(void)
{
    assert(radio_stream_bitrate_kbps_from_icy_header("128") == 128);
    assert(radio_stream_bitrate_kbps_from_icy_header("128 kbps") == 0);
    assert(radio_stream_bitrate_kbps_from_icy_header("0") == 0);
}

static void test_catalog_appends_missing_builtin_station_once(void)
{
    station_catalog_t catalog = {0};
    const station_catalog_entry_t europa = {
        .name = "Europa Plus",
        .url = "http://online-1.gkvr.ru:8000/europa_krd_128.aac",
        .flag = 0,
    };

    assert(station_catalog_append_if_missing(&catalog, &europa));
    assert(catalog.count == 1);
    assert(!station_catalog_append_if_missing(&catalog, &europa));
    assert(catalog.count == 1);
}

static void test_content_type_overrides_url_guess(void)
{
    radio_stream_format_t format = RADIO_STREAM_FORMAT_MP3;

    /* The cases the URL suffix cannot classify: an AAC stream on an
     * extensionless path, and FLAC-in-Ogg served as .ogg or as "flacm". */
    assert(radio_stream_format_from_content_type("audio/aac", &format));
    assert(format == RADIO_STREAM_FORMAT_AAC);
    format = RADIO_STREAM_FORMAT_MP3;
    assert(radio_stream_format_from_content_type("application/ogg", &format));
    assert(format == RADIO_STREAM_FORMAT_OGG_FLAC);
    format = RADIO_STREAM_FORMAT_MP3;
    assert(radio_stream_format_from_content_type("audio/ogg", &format));
    assert(format == RADIO_STREAM_FORMAT_OGG_FLAC);

    /* Parameters, whitespace and case are ignored. */
    format = RADIO_STREAM_FORMAT_AAC;
    assert(radio_stream_format_from_content_type("audio/mpeg;charset=UTF-8", &format));
    assert(format == RADIO_STREAM_FORMAT_MP3);
    format = RADIO_STREAM_FORMAT_AAC;
    assert(radio_stream_format_from_content_type("  AUDIO/MPEG ; charset=x", &format));
    assert(format == RADIO_STREAM_FORMAT_MP3);
    format = RADIO_STREAM_FORMAT_MP3;
    assert(radio_stream_format_from_content_type("audio/x-flac", &format));
    assert(format == RADIO_STREAM_FORMAT_FLAC);

    /* Unknown, empty and absent values leave the caller's guess alone. */
    format = RADIO_STREAM_FORMAT_AAC;
    assert(!radio_stream_format_from_content_type("text/html", &format));
    assert(format == RADIO_STREAM_FORMAT_AAC);
    assert(!radio_stream_format_from_content_type("application/octet-stream", &format));
    assert(format == RADIO_STREAM_FORMAT_AAC);
    assert(!radio_stream_format_from_content_type("", &format));
    assert(!radio_stream_format_from_content_type(";charset=utf-8", &format));
    assert(!radio_stream_format_from_content_type(NULL, &format));
    assert(format == RADIO_STREAM_FORMAT_AAC);
    assert(!radio_stream_format_from_content_type("audio/mpeg", NULL));

    /* A prefix of a known type must not match. */
    format = RADIO_STREAM_FORMAT_MP3;
    assert(!radio_stream_format_from_content_type("audio/aacx", &format));
    assert(!radio_stream_format_from_content_type("audio/", &format));
    assert(format == RADIO_STREAM_FORMAT_MP3);
}

int main(void)
{
    test_aac_url_selects_aac_decoder();
    test_content_type_overrides_url_guess();
    test_mp3_and_unknown_urls_keep_mp3_decoder();
    test_ogg_flac_url_selects_ogg_container_decoder();
    test_the_codec_inside_an_ogg_stream_is_read_from_its_first_page();
    test_an_unreadable_ogg_page_keeps_the_earlier_guess();
    test_every_ogg_codec_names_itself_and_shares_one_decoder();
    test_icy_bitrate_header_is_parsed();
    test_catalog_appends_missing_builtin_station_once();
    puts("radio_stream_format tests passed");
    return 0;
}
