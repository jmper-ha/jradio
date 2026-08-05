#pragma once

typedef enum {
    RADIO_STREAM_FORMAT_MP3 = 0,
    RADIO_STREAM_FORMAT_AAC,
} radio_stream_format_t;

radio_stream_format_t radio_stream_format_from_url(const char *url);
const char *radio_stream_format_raw_uri(radio_stream_format_t format);
const char *radio_stream_format_codec_name(radio_stream_format_t format);
