#include "ts_demux.h"

#include <string.h>
#include <strings.h>

#define TS_SYNC_BYTE 0x47U
#define TS_PID_PAT 0x0000U
#define TS_TABLE_PAT 0x00U
#define TS_TABLE_PMT 0x02U

void ts_demux_init(ts_demux_t *demux)
{
    if (demux != NULL) memset(demux, 0, sizeof(*demux));
}

bool ts_uri_is_transport_stream(const char *uri)
{
    if (uri == NULL) return false;
    size_t length = strcspn(uri, "?#");
    return length >= 3U && strncasecmp(uri + length - 3U, ".ts", 3U) == 0;
}

/* The section a PSI payload starts, past its pointer field. Tables here are
 * a few dozen bytes, so one that does not fit its packet is not worth piecing
 * together - the next copy comes with the next segment. */
static const uint8_t *ts_section(const uint8_t *payload, size_t length, uint8_t table_id,
                                 size_t *section_length)
{
    if (length < 1U) return NULL;
    const size_t pointer = payload[0];
    if (1U + pointer + 3U > length) return NULL;
    const uint8_t *section = payload + 1U + pointer;
    if (section[0] != table_id) return NULL;
    const size_t declared = (size_t)((section[1] & 0x0FU) << 8 | section[2]);
    // Header, then the body the length counts, of which the last 4 are CRC.
    if (declared < 9U || 3U + declared > length - 1U - pointer) return NULL;
    *section_length = declared;
    return section;
}

static void ts_parse_pat(ts_demux_t *demux, const uint8_t *payload, size_t length)
{
    size_t section_length = 0U;
    const uint8_t *section = ts_section(payload, length, TS_TABLE_PAT, &section_length);
    if (section == NULL) return;
    const size_t end = 3U + section_length - 4U;
    for (size_t at = 8U; at + 4U <= end; at += 4U) {
        const uint16_t program = (uint16_t)(section[at] << 8 | section[at + 1U]);
        // Program 0 names the network table, not a program.
        if (program == 0U) continue;
        demux->pmt_pid = (uint16_t)((section[at + 2U] & 0x1FU) << 8 | section[at + 3U]);
        return;
    }
}

static bool ts_stream_type_is_audio(uint8_t type)
{
    return type == TS_STREAM_TYPE_AAC_ADTS || type == TS_STREAM_TYPE_MPEG1_AUDIO ||
           type == TS_STREAM_TYPE_MPEG2_AUDIO;
}

static void ts_parse_pmt(ts_demux_t *demux, const uint8_t *payload, size_t length)
{
    size_t section_length = 0U;
    const uint8_t *section = ts_section(payload, length, TS_TABLE_PMT, &section_length);
    if (section == NULL) return;
    const size_t end = 3U + section_length - 4U;
    const size_t program_info = (size_t)((section[10] & 0x0FU) << 8 | section[11]);
    for (size_t at = 12U + program_info; at + 5U <= end;) {
        const uint8_t type = section[at];
        const uint16_t pid = (uint16_t)((section[at + 1U] & 0x1FU) << 8 | section[at + 2U]);
        const size_t info = (size_t)((section[at + 3U] & 0x0FU) << 8 | section[at + 4U]);
        // The first stream the decoder can play; an ID3 timed-metadata
        // stream rides alongside on these servers and is passed over.
        if (ts_stream_type_is_audio(type)) {
            if (pid != demux->audio_pid) demux->pes_skip = 0U;
            demux->audio_pid = pid;
            demux->stream_type = type;
            return;
        }
        at += 5U + info;
    }
}

/* Audio out of one payload of the audio PID. A payload that starts a PES
 * packet begins with its header, which is not audio. */
static size_t ts_audio_payload(ts_demux_t *demux, const uint8_t *payload, size_t length,
                               bool unit_start, uint8_t *out)
{
    size_t at = 0U;
    if (unit_start) {
        if (length < 9U || payload[0] != 0U || payload[1] != 0U || payload[2] != 1U) {
            // Not a PES start after all: better a gap the decoder resyncs
            // across than a header fed to it as audio.
            demux->pes_skip = 0U;
            return 0U;
        }
        demux->pes_skip = 9U + (size_t)payload[8];
    }
    const size_t skip = demux->pes_skip < length ? demux->pes_skip : length;
    demux->pes_skip -= skip;
    at = skip;
    memcpy(out, payload + at, length - at);
    return length - at;
}

static size_t ts_packet(ts_demux_t *demux, const uint8_t *packet, uint8_t *out)
{
    const bool unit_start = (packet[1] & 0x40U) != 0U;
    const uint16_t pid = (uint16_t)((packet[1] & 0x1FU) << 8 | packet[2]);
    const uint8_t adaptation = (uint8_t)((packet[3] >> 4) & 0x03U);
    if ((adaptation & 0x01U) == 0U) return 0U;  // no payload
    size_t at = 4U;
    if ((adaptation & 0x02U) != 0U) {
        at += 1U + (size_t)packet[4];
        if (at >= TS_PACKET_SIZE) return 0U;
    }
    const uint8_t *payload = packet + at;
    const size_t length = TS_PACKET_SIZE - at;

    if (pid == TS_PID_PAT) {
        if (unit_start) ts_parse_pat(demux, payload, length);
        return 0U;
    }
    if (demux->pmt_pid != 0U && pid == demux->pmt_pid) {
        if (unit_start) ts_parse_pmt(demux, payload, length);
        return 0U;
    }
    if (demux->audio_pid != 0U && pid == demux->audio_pid) {
        return ts_audio_payload(demux, payload, length, unit_start, out);
    }
    return 0U;
}

size_t ts_demux_feed(ts_demux_t *demux, const uint8_t *input, size_t length, size_t *consumed,
                     uint8_t *out, size_t out_size)
{
    size_t used = 0U;
    size_t written = 0U;
    if (demux == NULL || input == NULL || out == NULL) {
        if (consumed != NULL) *consumed = 0U;
        return 0U;
    }
    while (used < length && out_size - written >= TS_PAYLOAD_MAX) {
        if (demux->carry_length == 0U) {
            // Between packets: anything but a sync byte is a packet lost in
            // the middle, and the way back is the next sync byte.
            if (input[used] != TS_SYNC_BYTE) {
                ++demux->sync_losses;
                const uint8_t *next = memchr(input + used, TS_SYNC_BYTE, length - used);
                used = next != NULL ? (size_t)(next - input) : length;
                continue;
            }
            if (length - used >= TS_PACKET_SIZE) {
                written += ts_packet(demux, input + used, out + written);
                used += TS_PACKET_SIZE;
                continue;
            }
        }
        const size_t need = TS_PACKET_SIZE - demux->carry_length;
        const size_t take = need < length - used ? need : length - used;
        memcpy(demux->carry + demux->carry_length, input + used, take);
        demux->carry_length += take;
        used += take;
        if (demux->carry_length == TS_PACKET_SIZE) {
            written += ts_packet(demux, demux->carry, out + written);
            demux->carry_length = 0U;
        }
    }
    if (consumed != NULL) *consumed = used;
    return written;
}
