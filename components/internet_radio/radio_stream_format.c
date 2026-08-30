#include "radio_stream_format.h"

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

radio_stream_format_t radio_stream_format_from_url(const char *url)
{
    if (url == NULL) return RADIO_STREAM_FORMAT_MP3;

    const size_t url_length = strlen(url);
    const char *extension = strrchr(url, '.');
    if (extension != NULL && strncasecmp(extension, ".aac", 4) == 0 &&
        (extension[4] == '\0' || extension[4] == '?' || extension[4] == '#')) {
        return RADIO_STREAM_FORMAT_AAC;
    }
    if (url_length >= 5U && strncasecmp(url + url_length - 5U, "-flac", 5) == 0) {
        return RADIO_STREAM_FORMAT_OGG_FLAC;
    }
    if (extension != NULL && strncasecmp(extension, ".flac", 5) == 0 &&
        (extension[5] == '\0' || extension[5] == '?' || extension[5] == '#')) {
        return RADIO_STREAM_FORMAT_FLAC;
    }
    return RADIO_STREAM_FORMAT_MP3;
}

bool radio_stream_format_is_ogg(radio_stream_format_t format)
{
    return format == RADIO_STREAM_FORMAT_OGG_FLAC ||
           format == RADIO_STREAM_FORMAT_OGG_VORBIS || format == RADIO_STREAM_FORMAT_OGG_OPUS;
}

radio_stream_format_t radio_stream_format_from_ogg_page(const uint8_t *data, size_t length,
                                                        radio_stream_format_t fallback)
{
    /* Signature, its length, and the codec it names. The identification header
     * sits alone in the first page of a stream, so only that page has to be
     * understood - the codec never changes afterwards. */
    static const struct {
        const char *signature;
        size_t length;
        radio_stream_format_t format;
    } codecs[] = {
        /* Split so the "F" is not swallowed as a fourth hex digit. */
        {"\x7f" "FLAC", 5U, RADIO_STREAM_FORMAT_OGG_FLAC},
        {"\x01vorbis", 7U, RADIO_STREAM_FORMAT_OGG_VORBIS},
        {"OpusHead", 8U, RADIO_STREAM_FORMAT_OGG_OPUS},
    };
    /* A page header is "OggS", version, flags, granule, serial, sequence and
     * CRC, then the segment count - after which one byte per segment stands
     * between the header and the body. */
    static const size_t header_bytes = 27U;

    if (data == NULL || length <= header_bytes) return fallback;
    if (memcmp(data, "OggS", 4U) != 0) return fallback;
    const size_t body = header_bytes + (size_t)data[26];
    if (body >= length) return fallback;

    const uint8_t *page = data + body;
    const size_t page_length = length - body;
    for (size_t index = 0U; index < sizeof(codecs) / sizeof(codecs[0]); ++index) {
        if (page_length >= codecs[index].length &&
            memcmp(page, codecs[index].signature, codecs[index].length) == 0) {
            return codecs[index].format;
        }
    }
    return fallback;
}

const char *radio_stream_format_raw_uri(radio_stream_format_t format)
{
    if (format == RADIO_STREAM_FORMAT_AAC) return "raw://radio/stream.aac";
    if (format == RADIO_STREAM_FORMAT_FLAC) return "raw://radio/stream.flac";
    if (radio_stream_format_is_ogg(format)) return "raw://radio/stream.ogg";
    return "raw://radio/stream.mp3";
}

bool radio_stream_format_from_content_type(const char *content_type,
                                           radio_stream_format_t *format)
{
    static const struct {
        const char *media_type;
        radio_stream_format_t format;
    } known[] = {
        {"audio/mpeg", RADIO_STREAM_FORMAT_MP3},
        {"audio/mp3", RADIO_STREAM_FORMAT_MP3},
        {"audio/x-mpeg", RADIO_STREAM_FORMAT_MP3},
        {"audio/aac", RADIO_STREAM_FORMAT_AAC},
        {"audio/aacp", RADIO_STREAM_FORMAT_AAC},
        {"audio/x-aac", RADIO_STREAM_FORMAT_AAC},
        {"audio/flac", RADIO_STREAM_FORMAT_FLAC},
        {"audio/x-flac", RADIO_STREAM_FORMAT_FLAC},
        /* Ogg carries FLAC on the stations this project targets. Vorbis
         * announces the same media types and has no decoder here, so it stays
         * broken either way - only the error it produces changes. */
        {"audio/ogg", RADIO_STREAM_FORMAT_OGG_FLAC},
        {"application/ogg", RADIO_STREAM_FORMAT_OGG_FLAC},
        {"audio/x-ogg", RADIO_STREAM_FORMAT_OGG_FLAC},
    };

    if (content_type == NULL || format == NULL) return false;

    while (*content_type == ' ' || *content_type == '\t') ++content_type;
    size_t length = 0U;
    while (content_type[length] != '\0' && content_type[length] != ';' &&
           content_type[length] != ' ' && content_type[length] != '\t') {
        ++length;
    }
    if (length == 0U) return false;

    for (size_t index = 0U; index < sizeof(known) / sizeof(known[0]); ++index) {
        if (strlen(known[index].media_type) == length &&
            strncasecmp(content_type, known[index].media_type, length) == 0) {
            *format = known[index].format;
            return true;
        }
    }
    return false;
}

const char *radio_stream_format_codec_name(radio_stream_format_t format)
{
    if (format == RADIO_STREAM_FORMAT_AAC) return "AAC";
    if (format == RADIO_STREAM_FORMAT_OGG_VORBIS) return "Vorbis";
    if (format == RADIO_STREAM_FORMAT_OGG_OPUS) return "Opus";
    if (format == RADIO_STREAM_FORMAT_FLAC || format == RADIO_STREAM_FORMAT_OGG_FLAC) {
        return "FLAC";
    }
    return "MP3";
}

uint16_t radio_stream_bitrate_kbps_from_icy_header(const char *value)
{
    char *end;
    unsigned long bitrate;

    if (value == NULL || value[0] == '\0') return 0;
    errno = 0;
    bitrate = strtoul(value, &end, 10);
    if (errno != 0 || *end != '\0' || bitrate == 0U || bitrate > UINT16_MAX) return 0;

    return (uint16_t)bitrate;
}
