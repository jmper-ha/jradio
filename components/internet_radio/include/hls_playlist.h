#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* HLS (RFC 8216) playlist handling, the parsing half.
 *
 * A station URL ending in .m3u8 is not a stream at all: it is a text index of
 * short files. Playing one means fetching the index, fetching each segment it
 * names, and re-fetching the index as it slides forward - a loop the plain
 * "open a socket and read forever" path cannot express. Everything here is the
 * part of that loop with no network in it, so it can be tested on the host.
 *
 * Sizes are fixed rather than allocated: this runs from the radio task, which
 * already holds its buffers in PSRAM, and a parser that can fail on malloc in
 * the middle of a live stream is worse than one that drops a long URI. */

/* Room for one URI as written in a playlist, or one resolved absolute URL.
 * Segment names are short (~30 chars here) but variant URIs carry session
 * tokens in a query string, and CDN paths can be long. */
#define HLS_URI_MAX 200U
/* Segments kept from one playlist. The window is typically 6-10 segments; a
 * longer one just means the oldest entries are dropped, and those are the ones
 * already too far behind the live edge to be worth playing. */
#define HLS_SEGMENT_MAX 16U

typedef struct {
    char uri[HLS_URI_MAX];
    uint32_t duration_ms;
} hls_segment_t;

typedef struct {
    /* A master playlist lists variant streams instead of segments; the caller
     * has to fetch `variant_uri` and parse again. */
    bool is_master;
    char variant_uri[HLS_URI_MAX];
    uint32_t variant_bandwidth;

    uint32_t target_duration_ms;
    /* Media sequence number of segments[0]. The only stable identity a segment
     * has: names change, positions shift, this counts. */
    uint64_t media_sequence;
    /* Present on a finished recording, absent on a live stream. */
    bool has_endlist;
    size_t segment_count;
    hls_segment_t segments[HLS_SEGMENT_MAX];
} hls_playlist_t;

/* True for a URL that names an HLS playlist rather than a stream. A query
 * string or fragment after the extension is ignored, since session tokens
 * routinely follow it. */
bool hls_url_is_playlist(const char *url);

/* Parses one playlist. Returns false only for input that is not a playlist at
 * all (no #EXTM3U); a playlist with no usable segments parses successfully
 * with segment_count == 0, because that is a live stream between refreshes,
 * not an error. */
bool hls_playlist_parse(const char *text, size_t length, hls_playlist_t *out);

/* Resolves a playlist reference against the URL it was found in, covering the
 * three forms playlists actually use: absolute URL, absolute path, and a
 * sibling name. Returns false if the result would not fit, rather than
 * truncating into a URL that would fetch the wrong thing. */
bool hls_url_resolve(const char *base, const char *reference, char *out, size_t out_size);

/* Where to start playing a live playlist: far enough back from the live edge
 * to survive the jitter that made the server publish segments this size, near
 * enough that the stream is still live. RFC 8216 puts it at three target
 * durations from the end. */
uint64_t hls_playlist_live_start(const hls_playlist_t *playlist);

/* Index of the segment carrying `sequence`, or HLS_SEGMENT_NONE if the window
 * no longer holds it. A caller that fell behind gets HLS_SEGMENT_NONE and must
 * rejoin at media_sequence; a caller that is up to date gets it while waiting
 * for the next refresh to publish more. */
#define HLS_SEGMENT_NONE ((size_t)-1)
size_t hls_playlist_index_of(const hls_playlist_t *playlist, uint64_t sequence);

/* Length of the ID3v2 tag at the start of a buffer, or 0 if there is none.
 *
 * Every segment of an elementary-AAC HLS stream starts with an ID3 tag holding
 * the Apple transport timestamp, before the first ADTS frame. The AAC decoder
 * is handed a byte stream and expects to find a sync word, so the tag has to
 * come off first - and it can only be done here, where segment boundaries are
 * known, since mid-stream the bytes are indistinguishable from audio.
 *
 * Returns 0 when the buffer is too short to hold a complete ID3 header; the
 * caller must present at least the first 10 bytes of a segment. */
size_t hls_id3_prefix_length(const uint8_t *data, size_t length);
