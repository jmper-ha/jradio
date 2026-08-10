#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    RADIO_STREAM_FORMAT_MP3 = 0,
    RADIO_STREAM_FORMAT_AAC,
    RADIO_STREAM_FORMAT_FLAC,
    RADIO_STREAM_FORMAT_OGG_FLAC,
} radio_stream_format_t;

radio_stream_format_t radio_stream_format_from_url(const char *url);
/* Maps a Content-Type header value to a decoder. Parameters (";charset=...")
 * and surrounding whitespace are ignored, matching is case-insensitive.
 * Returns false and leaves *format untouched for types this build cannot
 * decode or does not recognise, so the caller can keep its URL-based guess. */
bool radio_stream_format_from_content_type(const char *content_type,
                                           radio_stream_format_t *format);
const char *radio_stream_format_raw_uri(radio_stream_format_t format);
const char *radio_stream_format_codec_name(radio_stream_format_t format);
uint16_t radio_stream_bitrate_kbps_from_icy_header(const char *value);
