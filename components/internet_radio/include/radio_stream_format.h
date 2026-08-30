#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* radio_decoder.cpp is C++ and calls into this. */
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RADIO_STREAM_FORMAT_MP3 = 0,
    RADIO_STREAM_FORMAT_AAC,
    RADIO_STREAM_FORMAT_FLAC,
    /* One Ogg stream can carry any of three codecs, and the same decoder plays
     * all three - these are apart only so the name shown on the panel and in
     * the web player is the codec that is actually playing. Which one it is
     * cannot be known from the Content-Type, which says "audio/ogg" for every
     * one of them; it is read from the first page of the body. */
    RADIO_STREAM_FORMAT_OGG_FLAC,
    RADIO_STREAM_FORMAT_OGG_VORBIS,
    RADIO_STREAM_FORMAT_OGG_OPUS,
} radio_stream_format_t;

radio_stream_format_t radio_stream_format_from_url(const char *url);
/* Maps a Content-Type header value to a decoder. Parameters (";charset=...")
 * and surrounding whitespace are ignored, matching is case-insensitive.
 * Returns false and leaves *format untouched for types this build cannot
 * decode or does not recognise, so the caller can keep its URL-based guess. */
bool radio_stream_format_from_content_type(const char *content_type,
                                           radio_stream_format_t *format);
bool radio_stream_format_is_ogg(radio_stream_format_t format);

/* Names the codec inside an Ogg stream from the start of its body.
 *
 * The first page of an Ogg stream is the codec's identification header, and
 * its first bytes name the codec. `fallback` is returned when `data` does not
 * start on a page boundary or holds a codec this build cannot decode, so a
 * caller can hand over whatever it had guessed and let the guess stand. */
radio_stream_format_t radio_stream_format_from_ogg_page(const uint8_t *data, size_t length,
                                                        radio_stream_format_t fallback);
const char *radio_stream_format_raw_uri(radio_stream_format_t format);
const char *radio_stream_format_codec_name(radio_stream_format_t format);
uint16_t radio_stream_bitrate_kbps_from_icy_header(const char *value);

#ifdef __cplusplus
}
#endif
