#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ts_demux.h"

/* Packets built the way the Retro FM segments are laid out (checked against
 * one, 2026-10-26): PAT, a PMT naming an ID3 metadata stream before the AAC
 * one, then the audio as PES packets whose tail is padded with an adaptation
 * field. */

#define PMT_PID 256U
#define AUDIO_PID 257U
#define ID3_PID 258U

static size_t g_length;
static unsigned char g_stream[TS_PACKET_SIZE * 16U];

static unsigned char *packet_start(unsigned pid, int unit_start, size_t payload_length)
{
    unsigned char *packet = g_stream + g_length;
    g_length += TS_PACKET_SIZE;
    memset(packet, 0xFF, TS_PACKET_SIZE);
    packet[0] = 0x47U;
    packet[1] = (unsigned char)((unit_start ? 0x40U : 0U) | ((pid >> 8) & 0x1FU));
    packet[2] = (unsigned char)(pid & 0xFFU);
    if (payload_length >= TS_PAYLOAD_MAX) {
        packet[3] = 0x10U;
        return packet + 4U;
    }
    // A short payload is pushed to the end by an adaptation field of stuffing.
    packet[3] = 0x30U;
    const size_t field = TS_PAYLOAD_MAX - payload_length - 1U;
    packet[4] = (unsigned char)field;
    if (field > 0U) packet[5] = 0x00U;
    return packet + 5U + field;
}

static void add_pat(void)
{
    static const unsigned char section[] = {
        0x00, 0x00, 0xB0, 0x0D, 0x00, 0x01, 0xC1, 0x00, 0x00,
        0x00, 0x01, 0xE1, 0x00, // program 1 -> PMT PID 256
        0xDE, 0xAD, 0xBE, 0xEF, // CRC, not checked
    };
    memcpy(packet_start(0U, 1, sizeof(section)), section, sizeof(section));
}

static void add_pmt(void)
{
    static const unsigned char section[] = {
        0x00, 0x02, 0xB0, 0x1C, 0x00, 0x01, 0xC1, 0x00, 0x00,
        0xE1, 0x01,             // PCR PID 257
        0xF0, 0x00,             // no program info
        0x15, 0xE1, 0x02, 0xF0, 0x00, // ID3 timed metadata on 258
        0x0F, 0xE1, 0x01, 0xF0, 0x00, // ADTS AAC on 257
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // padding to the length
        0xDE, 0xAD, 0xBE, 0xEF,
    };
    unsigned char copy[sizeof(section)];
    memcpy(copy, section, sizeof(section));
    copy[3] = (unsigned char)(sizeof(section) - 1U - 3U);
    memcpy(packet_start(PMT_PID, 1, sizeof(copy)), copy, sizeof(copy));
}

/* One PES packet of `length` audio bytes counting up from `first`, split
 * across as many TS packets as it takes. */
static void add_audio(unsigned char first, size_t length, size_t optional_header)
{
    unsigned char pes[600];
    size_t at = 0U;
    pes[at++] = 0x00;
    pes[at++] = 0x00;
    pes[at++] = 0x01;
    pes[at++] = 0xC0; // audio stream 0
    pes[at++] = 0x00;
    pes[at++] = 0x00;
    pes[at++] = 0x80;
    pes[at++] = 0x80; // PTS present
    pes[at++] = (unsigned char)optional_header;
    for (size_t i = 0U; i < optional_header; ++i) pes[at++] = 0x21;
    for (size_t i = 0U; i < length; ++i) pes[at++] = (unsigned char)(first + i);

    size_t sent = 0U;
    while (sent < at) {
        const size_t chunk = at - sent < TS_PAYLOAD_MAX ? at - sent : TS_PAYLOAD_MAX;
        memcpy(packet_start(AUDIO_PID, sent == 0U, chunk), pes + sent, chunk);
        sent += chunk;
    }
}

static void add_id3(void)
{
    static const unsigned char pes[] = {0x00, 0x00, 0x01, 0xBD, 0x00, 0x08, 0x80, 0x80,
                                        0x00, 'I',  'D',  '3',  0x04, 0x00};
    memcpy(packet_start(ID3_PID, 1, sizeof(pes)), pes, sizeof(pes));
}

static void build_segment(void)
{
    g_length = 0U;
    add_pat();
    add_pmt();
    add_id3();
    add_audio(0x00, 300U, 5U);
    add_audio(0x80, 100U, 5U);
}

static void expect_audio(const unsigned char *out, size_t length)
{
    assert(length == 400U);
    for (size_t i = 0U; i < 300U; ++i) assert(out[i] == (unsigned char)i);
    for (size_t i = 0U; i < 100U; ++i) assert(out[300U + i] == (unsigned char)(0x80U + i));
}

static size_t demux_in_chunks(ts_demux_t *demux, size_t chunk, unsigned char *out,
                              size_t out_size)
{
    size_t offset = 0U;
    size_t written = 0U;
    while (offset < g_length) {
        const size_t length = g_length - offset < chunk ? g_length - offset : chunk;
        size_t consumed = 0U;
        written += ts_demux_feed(demux, g_stream + offset, length, &consumed, out + written,
                                 out_size - written);
        assert(consumed > 0U);
        offset += consumed;
    }
    return written;
}

static void test_the_audio_comes_out_without_its_headers(void)
{
    build_segment();
    ts_demux_t demux;
    ts_demux_init(&demux);
    unsigned char out[1024];
    expect_audio(out, demux_in_chunks(&demux, g_length, out, sizeof(out)));
    assert(demux.pmt_pid == PMT_PID);
    // The ID3 stream is listed first and must not be taken for the audio.
    assert(demux.audio_pid == AUDIO_PID);
    assert(demux.stream_type == TS_STREAM_TYPE_AAC_ADTS);
    assert(demux.sync_losses == 0U);
}

static void test_any_chunking_gives_the_same_audio(void)
{
    build_segment();
    static const size_t chunks[] = {1U, 7U, 187U, 188U, 189U, 500U};
    for (size_t i = 0U; i < sizeof(chunks) / sizeof(chunks[0]); ++i) {
        ts_demux_t demux;
        ts_demux_init(&demux);
        unsigned char out[1024];
        expect_audio(out, demux_in_chunks(&demux, chunks[i], out, sizeof(out)));
    }
}

static void test_a_full_output_stops_the_feed_without_losing_bytes(void)
{
    build_segment();
    ts_demux_t demux;
    ts_demux_init(&demux);
    unsigned char out[1024];
    size_t offset = 0U;
    size_t written = 0U;
    while (offset < g_length) {
        // Room for exactly one packet's payload each call.
        size_t consumed = 0U;
        unsigned char window[TS_PAYLOAD_MAX];
        const size_t got = ts_demux_feed(&demux, g_stream + offset, g_length - offset,
                                         &consumed, window, sizeof(window));
        memcpy(out + written, window, got);
        written += got;
        offset += consumed;
    }
    expect_audio(out, written);

    // Less room than one payload takes nothing at all.
    ts_demux_init(&demux);
    size_t consumed = 99U;
    assert(ts_demux_feed(&demux, g_stream, g_length, &consumed, out, TS_PAYLOAD_MAX - 1U) ==
           0U);
    assert(consumed == 0U);
}

static void test_a_cut_packet_costs_that_packet_only(void)
{
    build_segment();
    add_audio(0xC0, 50U, 0U);
    // Drop 50 bytes from the middle of the first PES's second packet. The
    // short packet is completed from the start of the next one, so that one
    // is lost with it; everything after must come through once the next sync
    // byte is found.
    const size_t damaged = 4U * TS_PACKET_SIZE;
    memmove(g_stream + damaged + 50U, g_stream + damaged + 100U, g_length - damaged - 100U);
    g_length -= 50U;

    ts_demux_t demux;
    ts_demux_init(&demux);
    unsigned char out[1024];
    const size_t written = demux_in_chunks(&demux, 64U, out, sizeof(out));
    assert(demux.sync_losses >= 1U);
    // The third PES arrives whole.
    assert(written >= 50U);
    for (size_t i = 0U; i < 50U; ++i) {
        assert(out[written - 50U + i] == (unsigned char)(0xC0U + i));
    }
}

static void test_audio_before_the_tables_is_dropped(void)
{
    g_length = 0U;
    add_audio(0x00, 50U, 0U);
    add_pat();
    add_pmt();
    add_audio(0x40, 50U, 0U);
    ts_demux_t demux;
    ts_demux_init(&demux);
    unsigned char out[1024];
    const size_t written = demux_in_chunks(&demux, g_length, out, sizeof(out));
    assert(written == 50U);
    assert(out[0] == 0x40U);
}

static void test_a_transport_stream_is_told_by_its_name(void)
{
    assert(ts_uri_is_transport_stream("l0_6adfaa73870f61c0080f022f.ts"));
    assert(ts_uri_is_transport_stream("https://host/seg-1.TS?token=abc"));
    assert(!ts_uri_is_transport_stream("l0_6a834a545ce8a2c4b30a06d2.aac"));
    assert(!ts_uri_is_transport_stream("https://host/ts/segment.aac?x=.ts"));
    assert(!ts_uri_is_transport_stream(NULL));
}

int main(void)
{
    test_the_audio_comes_out_without_its_headers();
    test_any_chunking_gives_the_same_audio();
    test_a_full_output_stops_the_feed_without_losing_bytes();
    test_a_cut_packet_costs_that_packet_only();
    test_audio_before_the_tables_is_dropped();
    test_a_transport_stream_is_told_by_its_name();
    puts("test_ts_demux: ok");
    return 0;
}
