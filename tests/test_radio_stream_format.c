#include <assert.h>
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
    test_icy_bitrate_header_is_parsed();
    test_catalog_appends_missing_builtin_station_once();
    puts("radio_stream_format tests passed");
    return 0;
}
