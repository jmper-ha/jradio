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

    const char *extension = strrchr(url, '.');
    if (extension != NULL && strncasecmp(extension, ".aac", 4) == 0 &&
        (extension[4] == '\0' || extension[4] == '?' || extension[4] == '#')) {
        return RADIO_STREAM_FORMAT_AAC;
    }
    return RADIO_STREAM_FORMAT_MP3;
}

const char *radio_stream_format_raw_uri(radio_stream_format_t format)
{
    return format == RADIO_STREAM_FORMAT_AAC ? "raw://radio/stream.aac" :
                                               "raw://radio/stream.mp3";
}

const char *radio_stream_format_codec_name(radio_stream_format_t format)
{
    return format == RADIO_STREAM_FORMAT_AAC ? "AAC" : "MP3";
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
