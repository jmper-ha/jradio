#include "hls_playlist.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

bool hls_url_is_playlist(const char *url)
{
    if (url == NULL) return false;
    /* Search the path only: a query string can easily contain ".m3u8" as the
     * value of a parameter without the URL naming a playlist. */
    size_t path_length = 0U;
    while (url[path_length] != '\0' && url[path_length] != '?' && url[path_length] != '#') {
        ++path_length;
    }
    if (path_length < 5U) return false;
    return strncasecmp(url + path_length - 5U, ".m3u8", 5U) == 0;
}

/* One line of the playlist, trimmed. Playlists are served with either line
 * ending, and trailing whitespace on a URI line would corrupt the request. */
static size_t hls_line_copy(const char *text, size_t length, size_t offset, char *out,
                            size_t out_size, size_t *out_length)
{
    size_t end = offset;
    while (end < length && text[end] != '\n' && text[end] != '\r') ++end;

    size_t start = offset;
    while (start < end && (text[start] == ' ' || text[start] == '\t')) ++start;
    size_t stop = end;
    while (stop > start && (text[stop - 1U] == ' ' || text[stop - 1U] == '\t')) --stop;

    const size_t line_length = stop - start;
    if (line_length < out_size) {
        memcpy(out, text + start, line_length);
        out[line_length] = '\0';
        *out_length = line_length;
    } else {
        /* Too long to use. Report it as empty rather than truncating: a
         * half URI would fetch something, and something wrong is worse than
         * nothing. */
        out[0] = '\0';
        *out_length = 0U;
    }

    /* Step over the terminator, treating CRLF as one. */
    if (end < length && text[end] == '\r') ++end;
    if (end < length && text[end] == '\n') ++end;
    return end;
}

/* Value of an attribute in an EXT-X-STREAM-INF attribute list. Only the
 * unquoted numeric attributes are needed here. */
static uint32_t hls_attribute_number(const char *line, const char *name)
{
    const size_t name_length = strlen(name);
    for (const char *at = line; *at != '\0'; ++at) {
        if (strncasecmp(at, name, name_length) != 0) continue;
        if (at[name_length] != '=') continue;
        /* Must start an attribute, not end one: BANDWIDTH would otherwise
         * match inside AVERAGE-BANDWIDTH. */
        if (at != line && at[-1] != ',' && at[-1] != ':') continue;
        return (uint32_t)strtoul(at + name_length + 1U, NULL, 10);
    }
    return 0U;
}

/* "#EXTINF:5.990748," -> 5990. Fixed point rather than float: no FPU work on
 * the audio task, and milliseconds are finer than any use here needs. */
static uint32_t hls_duration_ms(const char *value)
{
    char *end = NULL;
    const unsigned long seconds = strtoul(value, &end, 10);
    uint32_t millis = (uint32_t)(seconds * 1000U);
    if (end != NULL && *end == '.') {
        uint32_t scale = 100U;
        for (const char *at = end + 1U; *at >= '0' && *at <= '9' && scale > 0U; ++at) {
            millis += (uint32_t)(*at - '0') * scale;
            scale /= 10U;
        }
    }
    return millis;
}

bool hls_playlist_parse(const char *text, size_t length, hls_playlist_t *out)
{
    if (text == NULL || out == NULL) return false;

    memset(out, 0, sizeof(*out));

    char line[HLS_URI_MAX];
    size_t line_length = 0U;
    size_t offset = hls_line_copy(text, length, 0U, line, sizeof(line), &line_length);
    if (strncmp(line, "#EXTM3U", 7U) != 0) return false;

    uint32_t pending_duration = 0U;
    /* A URI line counts as a segment only when an EXTINF introduced it. That
     * is what keeps a master playlist's variant URIs out of the segment list
     * even though both are bare lines. */
    bool pending_segment = false;
    uint32_t pending_bandwidth = 0U;
    bool pending_variant = false;
    bool have_variant = false;

    while (offset < length) {
        offset = hls_line_copy(text, length, offset, line, sizeof(line), &line_length);
        if (line_length == 0U) continue;

        if (line[0] == '#') {
            if (strncmp(line, "#EXTINF:", 8U) == 0) {
                pending_duration = hls_duration_ms(line + 8U);
                pending_segment = true;
            } else if (strncmp(line, "#EXT-X-STREAM-INF:", 18U) == 0) {
                pending_bandwidth = hls_attribute_number(line, "BANDWIDTH");
                pending_variant = true;
                out->is_master = true;
            } else if (strncmp(line, "#EXT-X-TARGETDURATION:", 22U) == 0) {
                out->target_duration_ms = (uint32_t)strtoul(line + 22U, NULL, 10) * 1000U;
            } else if (strncmp(line, "#EXT-X-MEDIA-SEQUENCE:", 22U) == 0) {
                out->media_sequence = strtoull(line + 22U, NULL, 10);
            } else if (strncmp(line, "#EXT-X-ENDLIST", 14U) == 0) {
                out->has_endlist = true;
            }
            continue;
        }

        if (pending_variant) {
            pending_variant = false;
            /* Lowest bitrate wins. This device decodes on one core alongside
             * the UI and holds a fixed input buffer sized in bytes, so the
             * cheapest variant is the one most likely to play without
             * dropouts - and on a small speaker the difference is inaudible. */
            if (!have_variant || pending_bandwidth < out->variant_bandwidth ||
                out->variant_bandwidth == 0U) {
                have_variant = true;
                out->variant_bandwidth = pending_bandwidth;
                snprintf(out->variant_uri, sizeof(out->variant_uri), "%s", line);
            }
            pending_bandwidth = 0U;
            continue;
        }

        if (!pending_segment) continue;
        pending_segment = false;
        if (out->segment_count < HLS_SEGMENT_MAX) {
            hls_segment_t *segment = &out->segments[out->segment_count];
            snprintf(segment->uri, sizeof(segment->uri), "%s", line);
            segment->duration_ms = pending_duration;
            ++out->segment_count;
        } else {
            /* The window is longer than the buffer. Drop the oldest, not the
             * newest: keeping the tail keeps the live edge reachable. */
            memmove(&out->segments[0], &out->segments[1],
                    sizeof(out->segments[0]) * (HLS_SEGMENT_MAX - 1U));
            hls_segment_t *segment = &out->segments[HLS_SEGMENT_MAX - 1U];
            snprintf(segment->uri, sizeof(segment->uri), "%s", line);
            segment->duration_ms = pending_duration;
            ++out->media_sequence;
        }
        pending_duration = 0U;
    }

    if (out->segment_count > 0U) out->is_master = false;
    return true;
}

bool hls_url_resolve(const char *base, const char *reference, char *out, size_t out_size)
{
    if (reference == NULL || out == NULL || out_size == 0U) return false;
    if (reference[0] == '\0') return false;

    if (strncasecmp(reference, "http://", 7U) == 0 ||
        strncasecmp(reference, "https://", 8U) == 0) {
        if (strlen(reference) >= out_size) return false;
        snprintf(out, out_size, "%s", reference);
        return true;
    }
    if (base == NULL) return false;

    /* Find the end of "scheme://authority". */
    const char *scheme_end = strstr(base, "://");
    if (scheme_end == NULL) return false;
    const char *authority = scheme_end + 3U;
    const char *path = strchr(authority, '/');
    const size_t root_length = path == NULL ? strlen(base) : (size_t)(path - base);

    if (reference[0] == '/') {
        if (root_length + strlen(reference) >= out_size) return false;
        memcpy(out, base, root_length);
        snprintf(out + root_length, out_size - root_length, "%s", reference);
        return true;
    }

    /* Sibling of the current document: everything up to and including the last
     * slash, with the query string dropped - it belongs to the document, not
     * to the directory. */
    size_t directory_length = root_length + 1U;
    if (path != NULL) {
        for (size_t index = 0U; base[index] != '\0'; ++index) {
            if (base[index] == '?' || base[index] == '#') break;
            if (base[index] == '/') directory_length = index + 1U;
        }
    }
    if (directory_length >= out_size) return false;
    if (directory_length + strlen(reference) >= out_size) return false;
    memcpy(out, base, directory_length);
    if (path == NULL) out[root_length] = '/';
    snprintf(out + directory_length, out_size - directory_length, "%s", reference);
    return true;
}

uint64_t hls_playlist_live_start(const hls_playlist_t *playlist)
{
    if (playlist == NULL || playlist->segment_count == 0U) return 0U;
    /* A finished recording has a real beginning; play it from there. */
    if (playlist->has_endlist) return playlist->media_sequence;

    /* Three segments back from the end, per RFC 8216. Fewer would put playback
     * at the live edge, where every refresh is a race against the server
     * publishing the next segment; many more would sit so far behind that the
     * window slides past before it is reached. */
    const size_t back = playlist->segment_count < 4U ? playlist->segment_count - 1U : 3U;
    return playlist->media_sequence + (playlist->segment_count - 1U - back);
}

size_t hls_playlist_index_of(const hls_playlist_t *playlist, uint64_t sequence)
{
    if (playlist == NULL || playlist->segment_count == 0U) return HLS_SEGMENT_NONE;
    if (sequence < playlist->media_sequence) return HLS_SEGMENT_NONE;
    const uint64_t index = sequence - playlist->media_sequence;
    if (index >= (uint64_t)playlist->segment_count) return HLS_SEGMENT_NONE;
    return (size_t)index;
}

size_t hls_id3_prefix_length(const uint8_t *data, size_t length)
{
    if (data == NULL || length < 10U) return 0U;
    if (data[0] != 'I' || data[1] != 'D' || data[2] != '3') return 0U;
    /* A version byte of 0xff is reserved and marks a false match. */
    if (data[3] == 0xffU || data[4] == 0xffU) return 0U;
    /* Syncsafe: seven bits per byte, so no byte can look like a sync word. A
     * high bit set anywhere means this is not a real tag. */
    for (size_t index = 6U; index < 10U; ++index) {
        if ((data[index] & 0x80U) != 0U) return 0U;
    }
    const size_t size = ((size_t)data[6] << 21U) | ((size_t)data[7] << 14U) |
                        ((size_t)data[8] << 7U) | (size_t)data[9];
    /* Bit 4 of the flags adds a 10-byte footer to the total. */
    const size_t footer = (data[5] & 0x10U) != 0U ? 10U : 0U;
    return 10U + size + footer;
}
