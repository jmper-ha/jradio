#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* MPEG transport stream, the audio half: what an HLS segment named .ts holds
 * instead of the bare ADTS the .aac ones do.
 *
 * Retro FM on the same emgsound servers as Relax FM sends its segments that
 * way, and handed straight to the decoder they are AAC cut by a 4-byte header
 * every 188 bytes. All that is needed to undo it is to find the audio stream
 * in the program tables and peel the packet and PES headers off its payload;
 * what comes out is the ADTS (or MPEG audio) the decoder already reads.
 *
 * A state machine over whole packets with one partial packet carried between
 * calls, so it runs on whatever chunks the network hands over and needs no
 * buffer the size of a segment. Pure, for the host tests. */

#define TS_PACKET_SIZE 188U
/* The most audio one packet can carry: the packet less its 4-byte header. */
#define TS_PAYLOAD_MAX (TS_PACKET_SIZE - 4U)

/* Stream types from ISO/IEC 13818-1 table 2-34 that the decoder can take. */
#define TS_STREAM_TYPE_NONE 0x00U
#define TS_STREAM_TYPE_MPEG1_AUDIO 0x03U
#define TS_STREAM_TYPE_MPEG2_AUDIO 0x04U
#define TS_STREAM_TYPE_AAC_ADTS 0x0FU

typedef struct {
    uint16_t pmt_pid;   // 0 until the PAT has named one
    uint16_t audio_pid; // 0 until the PMT has named one
    uint8_t stream_type;
    /* Bytes still to drop from the start of the PES payload: a PES header
     * whose optional fields ran past the end of its packet. */
    size_t pes_skip;
    uint8_t carry[TS_PACKET_SIZE];
    size_t carry_length;
    uint32_t sync_losses;
} ts_demux_t;

void ts_demux_init(ts_demux_t *demux);

/* Feeds `input` and writes the audio elementary stream it carries to `out`.
 * Stops early when `out` has no room for another packet's payload - that is
 * TS_PAYLOAD_MAX bytes - so the caller has to call again with the rest;
 * `*consumed` says how much of `input` was taken. Returns the bytes written. */
size_t ts_demux_feed(ts_demux_t *demux, const uint8_t *input, size_t length, size_t *consumed,
                     uint8_t *out, size_t out_size);

/* True for a segment URI whose name says transport stream; a query string
 * after the extension is ignored. Only a hint: see the two below. */
bool ts_uri_is_transport_stream(const char *uri);

/* Bytes from the start of a segment that carry the sync byte where two
 * packets in a row would start - which bare ADTS or MP3 (0xFF), an ID3 tag
 * ("ID3") or fMP4 (a box size) never do. Needs at least one byte past the
 * first packet to say yes; less is not enough to tell. */
#define TS_PROBE_BYTES (3U * TS_PACKET_SIZE)
bool ts_looks_like_transport_stream(const uint8_t *data, size_t length);

/* The audio stream type the program tables in these bytes name, as the
 * demultiplexer would find it - TS_STREAM_TYPE_NONE when they do not get as
 * far as the PMT. What picks the decoder, before any audio is decoded. */
uint8_t ts_probe_stream_type(const uint8_t *data, size_t length);
