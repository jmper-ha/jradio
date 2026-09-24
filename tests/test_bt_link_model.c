#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bt_link_model.h"

/* Frames are built the way the module builds them - with the same writer -
 * so a change to the wire format on one side shows up here on the other. */
static jbt_frame_t frame_of(uint8_t type, const uint8_t *payload, size_t length)
{
    const jbt_frame_t frame = {.type = type, .len = (uint16_t)length, .payload = payload};
    return frame;
}

static void test_a_status_frame_fills_the_snapshot(void)
{
    uint8_t payload[64];
    jbt_writer_t writer;
    jbt_writer_init(&writer, payload, sizeof(payload));
    const jbt_status_t status = {
        .mode = JBT_MODE_SINK, .connection = JBT_CONN_CONNECTED,
        .peer = {1, 2, 3, 4, 5, 6}, .codec = JBT_CODEC_SBC, .sample_rate = 48000U,
        .play = JBT_PLAY_PLAYING, .volume = 90,
    };
    assert(jbt_put_status(&writer, &status));
    assert(jbt_put_tlv_string(&writer, JBT_TAG_PEER_NAME, "Pixel"));

    bt_link_state_t state;
    bt_link_model_init(&state);
    const jbt_frame_t frame = frame_of(JBT_MSG_STATUS, payload, writer.length);
    assert(bt_link_model_apply(&state, &frame) == BT_LINK_CHANGED_STATUS);
    assert(state.status.connection == JBT_CONN_CONNECTED);
    assert(state.status.sample_rate == 48000U);
    assert(state.status.volume == 90);
    assert(strcmp(state.peer_name, "Pixel") == 0);

    /* A later STATUS without a name clears it, and the track with it: both
     * belong to the connection they came with, and a phone that left must
     * not leave its song on the screen. */
    snprintf(state.title, sizeof(state.title), "Song");
    snprintf(state.artist, sizeof(state.artist), "Band");
    state.position_ms = 1234U;
    const uint32_t revision_before = state.track_revision;
    jbt_writer_init(&writer, payload, sizeof(payload));
    jbt_status_t gone = status;
    gone.connection = JBT_CONN_NONE;
    memset(gone.peer, 0, sizeof(gone.peer));
    assert(jbt_put_status(&writer, &gone));
    const jbt_frame_t frame2 = frame_of(JBT_MSG_STATUS, payload, writer.length);
    assert(bt_link_model_apply(&state, &frame2) == (BT_LINK_CHANGED_STATUS | BT_LINK_CHANGED_TRACK));
    assert(state.peer_name[0] == '\0');
    assert(state.title[0] == '\0' && state.artist[0] == '\0' && state.position_ms == 0U);
    assert(state.track_revision == revision_before + 1U);
    /* And a STATUS with nothing to clear reports only itself. */
    assert(bt_link_model_apply(&state, &frame2) == BT_LINK_CHANGED_STATUS);

    /* Too short to be a STATUS: nothing changes. */
    const jbt_frame_t frame3 = frame_of(JBT_MSG_STATUS, payload, 3U);
    assert(bt_link_model_apply(&state, &frame3) == 0U);
    assert(state.status.connection == JBT_CONN_NONE);
}

static void test_a_track_replaces_the_previous_one_whole(void)
{
    uint8_t payload[JBT_PAYLOAD_MAX];
    jbt_writer_t writer;
    jbt_writer_init(&writer, payload, sizeof(payload));
    assert(jbt_put_tlv_string(&writer, JBT_TAG_TITLE, "Разговор"));
    assert(jbt_put_tlv_string(&writer, JBT_TAG_ARTIST, "Мумий Тролль"));
    assert(jbt_put_tlv_string(&writer, JBT_TAG_ALBUM, "Икра"));
    assert(jbt_put_tlv_u32(&writer, JBT_TAG_DURATION_MS, 214000U));
    /* A tag this build does not know, in the middle: skipped, and the rest
     * still read. */
    assert(jbt_put_tlv_bytes(&writer, 0x7E, "??", 2U));
    assert(jbt_put_tlv_u32(&writer, JBT_TAG_TRACK_NO, 4U));

    bt_link_state_t state;
    bt_link_model_init(&state);
    state.position_ms = 99000U;
    const jbt_frame_t frame = frame_of(JBT_MSG_TRACK, payload, writer.length);
    assert(bt_link_model_apply(&state, &frame) == BT_LINK_CHANGED_TRACK);
    assert(strcmp(state.title, "Разговор") == 0);
    assert(strcmp(state.artist, "Мумий Тролль") == 0);
    assert(strcmp(state.album, "Икра") == 0);
    assert(state.duration_ms == 214000U);
    assert(state.track_no == 4U);
    assert(state.track_revision == 1U);
    assert(state.position_ms == 0U);

    /* The next track names only a title: the old album must not survive. */
    jbt_writer_init(&writer, payload, sizeof(payload));
    assert(jbt_put_tlv_string(&writer, JBT_TAG_TITLE, "Untitled"));
    const jbt_frame_t frame2 = frame_of(JBT_MSG_TRACK, payload, writer.length);
    assert(bt_link_model_apply(&state, &frame2) == BT_LINK_CHANGED_TRACK);
    assert(strcmp(state.title, "Untitled") == 0);
    assert(state.artist[0] == '\0' && state.album[0] == '\0');
    assert(state.duration_ms == 0U);
    assert(state.track_revision == 2U);
}

static void test_the_small_frames_move_one_field_each(void)
{
    bt_link_state_t state;
    bt_link_model_init(&state);

    uint8_t position[4] = {0x10, 0x27, 0, 0}; /* 10000 */
    const jbt_frame_t pos = frame_of(JBT_MSG_POSITION, position, 4U);
    assert(bt_link_model_apply(&state, &pos) == BT_LINK_CHANGED_POSITION);
    assert(state.position_ms == 10000U);

    const uint8_t paused = JBT_PLAY_PAUSED;
    const jbt_frame_t play = frame_of(JBT_MSG_PLAY_STATE, &paused, 1U);
    assert(bt_link_model_apply(&state, &play) == BT_LINK_CHANGED_PLAY);
    assert(state.status.play == JBT_PLAY_PAUSED);
    const uint8_t bogus = 9;
    const jbt_frame_t play2 = frame_of(JBT_MSG_PLAY_STATE, &bogus, 1U);
    assert(bt_link_model_apply(&state, &play2) == 0U);
    assert(state.status.play == JBT_PLAY_PAUSED);

    const uint8_t loud = 200; /* clipped to the scale */
    const jbt_frame_t volume = frame_of(JBT_MSG_VOLUME, &loud, 1U);
    assert(bt_link_model_apply(&state, &volume) == BT_LINK_CHANGED_VOLUME);
    assert(state.status.volume == 127);

    const uint8_t ack[2] = {JBT_MODE_SINK, JBT_RESULT_OK};
    const jbt_frame_t mode_ack = frame_of(JBT_MSG_MODE_ACK, ack, 2U);
    assert(bt_link_model_apply(&state, &mode_ack) == BT_LINK_CHANGED_MODE_ACK);
    assert(state.mode_acks == 1U && state.acked_mode == JBT_MODE_SINK);
    assert(state.status.mode == JBT_MODE_SINK);

    uint8_t pong[5] = {1, 0, 2, 0x34, 0x12};
    const jbt_frame_t p = frame_of(JBT_MSG_PONG, pong, 5U);
    assert(bt_link_model_apply(&state, &p) == BT_LINK_CHANGED_PONG);
    assert(state.protocol == 1 && state.fw_major == 0 && state.fw_minor == 2 && state.fw_build == 0x1234);

    const uint8_t event[1] = {JBT_EVENT_DISCONNECTED};
    const jbt_frame_t e = frame_of(JBT_MSG_EVENT, event, 1U);
    assert(bt_link_model_apply(&state, &e) == BT_LINK_CHANGED_EVENT);
    assert(state.event == JBT_EVENT_DISCONNECTED && state.events == 1U);

    /* Frames the model does not keep change nothing. */
    const uint8_t ack2[2] = {7, 0};
    const jbt_frame_t a = frame_of(JBT_MSG_ACK, ack2, 2U);
    assert(bt_link_model_apply(&state, &a) == 0U);
}

static void test_a_log_frame_is_read_for_printing(void)
{
    uint8_t payload[64];
    jbt_writer_t writer;
    jbt_writer_init(&writer, payload, sizeof(payload));
    jbt_put_u8(&writer, 2);
    jbt_put_tlv_string(&writer, JBT_TAG_TEXT, "W (123) a2dp_sink: audio suspended");
    const jbt_frame_t frame = frame_of(JBT_MSG_LOG, payload, writer.length);
    uint8_t level = 0;
    char text[64];
    assert(bt_link_model_log(&frame, &level, text, sizeof(text)));
    assert(level == 2);
    assert(strcmp(text, "W (123) a2dp_sink: audio suspended") == 0);
    const jbt_frame_t other = frame_of(JBT_MSG_PING, NULL, 0U);
    assert(!bt_link_model_log(&other, &level, text, sizeof(text)));
}

static void test_the_volume_scales_round_trip(void)
{
    /* Every percent the knob can produce comes back as itself. */
    for (unsigned p = 0; p <= 100U; ++p) {
        assert(bt_link_volume_to_percent(bt_link_volume_to_module((uint8_t)p)) == p);
    }
    assert(bt_link_volume_to_module(100) == 127);
    assert(bt_link_volume_to_module(0) == 0);
    assert(bt_link_volume_to_percent(127) == 100);
}

static void test_a_cover_is_fetched_piece_by_piece_and_only_once(void)
{
    bt_link_state_t state;
    bt_link_model_init(&state);
    uint32_t offset = 99U;
    uint16_t length = 0U;
    assert(!bt_link_model_cover_wanted(&state, &offset, &length));

    /* 1200 bytes announced: three pieces, the last one short. */
    uint8_t info[13];
    jbt_writer_t writer;
    jbt_writer_init(&writer, info, sizeof(info));
    jbt_put_u32(&writer, 1200U);
    jbt_put_u8(&writer, JBT_IMAGE_JPEG);
    jbt_put_u32(&writer, 0xABCD1234U);
    jbt_put_u16(&writer, 0U);
    jbt_put_u16(&writer, 0U);
    const jbt_frame_t announce = frame_of(JBT_MSG_COVER_INFO, info, writer.length);
    assert(bt_link_model_apply(&state, &announce) == BT_LINK_CHANGED_COVER_INFO);
    assert(state.cover_size == 1200U && state.cover_hash == 0xABCD1234U && state.cover_revision == 1U);
    assert(bt_link_model_cover_wanted(&state, &offset, &length));
    assert(offset == 0U && length == BT_LINK_COVER_CHUNK);

    uint8_t piece[4U + BT_LINK_COVER_CHUNK];
    const uint8_t *bytes;
    size_t n;
    bool complete = true;
    /* A stale answer - the wrong offset - is refused and changes nothing. */
    memset(piece, 0, sizeof(piece));
    piece[0] = 0x10; /* offset 16 */
    jbt_frame_t frame = frame_of(JBT_MSG_COVER_DATA, piece, sizeof(piece));
    assert(!bt_link_model_cover_data(&state, &frame, &offset, &bytes, &n, &complete));
    assert(state.cover_received == 0U);

    piece[0] = 0;
    frame = frame_of(JBT_MSG_COVER_DATA, piece, sizeof(piece));
    assert(bt_link_model_cover_data(&state, &frame, &offset, &bytes, &n, &complete));
    assert(offset == 0U && n == BT_LINK_COVER_CHUNK && !complete);
    assert(bytes == &piece[4]);
    assert(bt_link_model_cover_wanted(&state, &offset, &length));
    assert(offset == BT_LINK_COVER_CHUNK);

    /* Second piece, then the short last one. */
    memcpy(piece, &offset, 4U);
    frame = frame_of(JBT_MSG_COVER_DATA, piece, sizeof(piece));
    assert(bt_link_model_cover_data(&state, &frame, &offset, &bytes, &n, &complete) && !complete);
    assert(bt_link_model_cover_wanted(&state, &offset, &length));
    assert(offset == 2U * BT_LINK_COVER_CHUNK && length == 1200U - 2U * BT_LINK_COVER_CHUNK);
    memcpy(piece, &offset, 4U);
    frame = frame_of(JBT_MSG_COVER_DATA, piece, 4U + length);
    assert(bt_link_model_cover_data(&state, &frame, &offset, &bytes, &n, &complete));
    assert(complete && n == length);
    assert(state.cover_done_hash == 0xABCD1234U);
    /* Done: nothing more is wanted, and the same cover announced again -
     * the next chapter of a podcast - is not fetched again. */
    assert(!bt_link_model_cover_wanted(&state, &offset, &length));
    assert(bt_link_model_apply(&state, &announce) == BT_LINK_CHANGED_COVER_INFO);
    assert(!bt_link_model_cover_wanted(&state, &offset, &length));

    /* The phone's player closed: the picture is not wanted, and once the
     * phone plays again it is fetched back; a pause holds it. */
    const uint8_t stopped[1] = {JBT_PLAY_STOPPED};
    const jbt_frame_t stop = frame_of(JBT_MSG_PLAY_STATE, stopped, 1U);
    assert(bt_link_model_apply(&state, &stop) == BT_LINK_CHANGED_PLAY);
    assert(state.cover_held && state.cover_done_hash == 0U);
    assert(!bt_link_model_cover_wanted(&state, &offset, &length));
    const uint8_t playing[1] = {JBT_PLAY_PLAYING};
    const jbt_frame_t play = frame_of(JBT_MSG_PLAY_STATE, playing, 1U);
    assert(bt_link_model_apply(&state, &play) == BT_LINK_CHANGED_PLAY);
    assert(!state.cover_held);
    assert(bt_link_model_cover_wanted(&state, &offset, &length) && offset == 0U);
    const uint8_t paused[1] = {JBT_PLAY_PAUSED};
    const jbt_frame_t pause = frame_of(JBT_MSG_PLAY_STATE, paused, 1U);
    assert(bt_link_model_apply(&state, &pause) == BT_LINK_CHANGED_PLAY);
    assert(bt_link_model_cover_wanted(&state, &offset, &length));
    /* Stopped again, then a new picture announced by a new player: shown. */
    assert(bt_link_model_apply(&state, &stop) == BT_LINK_CHANGED_PLAY);
    assert(!bt_link_model_cover_wanted(&state, &offset, &length));

    /* A different cover is; one the module says has no bytes is not; one
     * larger than the host will hold is not either. */
    jbt_writer_init(&writer, info, sizeof(info));
    jbt_put_u32(&writer, 300U);
    jbt_put_u8(&writer, JBT_IMAGE_PNG);
    jbt_put_u32(&writer, 0x5555U);
    jbt_put_u16(&writer, 0U);
    jbt_put_u16(&writer, 0U);
    const jbt_frame_t other = frame_of(JBT_MSG_COVER_INFO, info, writer.length);
    assert(bt_link_model_apply(&state, &other) == BT_LINK_CHANGED_COVER_INFO);
    assert(bt_link_model_cover_wanted(&state, &offset, &length) && offset == 0U && length == 300U);
    jbt_writer_init(&writer, info, sizeof(info));
    jbt_put_u32(&writer, 0U);
    jbt_put_u8(&writer, JBT_IMAGE_JPEG);
    jbt_put_u32(&writer, 0U);
    const jbt_frame_t none = frame_of(JBT_MSG_COVER_INFO, info, writer.length);
    assert(bt_link_model_apply(&state, &none) == BT_LINK_CHANGED_COVER_INFO);
    assert(!bt_link_model_cover_wanted(&state, &offset, &length));
    jbt_writer_init(&writer, info, sizeof(info));
    jbt_put_u32(&writer, BT_LINK_COVER_MAX + 1U);
    jbt_put_u8(&writer, JBT_IMAGE_JPEG);
    jbt_put_u32(&writer, 7U);
    const jbt_frame_t huge = frame_of(JBT_MSG_COVER_INFO, info, writer.length);
    assert(bt_link_model_apply(&state, &huge) == BT_LINK_CHANGED_COVER_INFO);
    assert(!bt_link_model_cover_wanted(&state, &offset, &length));
}

static void test_a_scan_lists_each_speaker_once_and_keeps_its_name(void)
{
    bt_link_scan_t scan;
    bt_link_scan_init(&scan);
    uint8_t payload[64];
    jbt_writer_t writer;
    const uint8_t jbl[6] = {0x3D, 0xAB, 0x55, 0xFA, 0x58, 0xFC};
    const uint8_t other[6] = {1, 2, 3, 4, 5, 6};

    jbt_writer_init(&writer, payload, sizeof(payload));
    jbt_put_bytes(&writer, jbl, 6U);
    jbt_put_u8(&writer, (uint8_t)-60);
    jbt_put_u32(&writer, 0x240404U);
    jbt_put_tlv_string(&writer, JBT_TAG_NAME, "JBL Flip");
    jbt_frame_t frame = frame_of(JBT_MSG_SCAN_RESULT, payload, writer.length);
    assert(bt_link_scan_apply(&scan, &frame));
    assert(scan.count == 1U && scan.revision == 1U);
    assert(strcmp(scan.found[0].name, "JBL Flip") == 0 && scan.found[0].rssi == -60);

    /* Seen again, closer, without a name: the signal updates, the name stays. */
    jbt_writer_init(&writer, payload, sizeof(payload));
    jbt_put_bytes(&writer, jbl, 6U);
    jbt_put_u8(&writer, (uint8_t)-50);
    jbt_put_u32(&writer, 0x240404U);
    frame = frame_of(JBT_MSG_SCAN_RESULT, payload, writer.length);
    assert(bt_link_scan_apply(&scan, &frame));
    assert(scan.count == 1U && scan.found[0].rssi == -50);
    assert(strcmp(scan.found[0].name, "JBL Flip") == 0);

    jbt_writer_init(&writer, payload, sizeof(payload));
    jbt_put_bytes(&writer, other, 6U);
    jbt_put_u8(&writer, (uint8_t)-80);
    jbt_put_u32(&writer, 0x240404U);
    frame = frame_of(JBT_MSG_SCAN_RESULT, payload, writer.length);
    assert(bt_link_scan_apply(&scan, &frame));
    assert(scan.count == 2U && scan.found[1].name[0] == '\0');

    /* Too short to be a result; a frame of another type. */
    frame = frame_of(JBT_MSG_SCAN_RESULT, payload, 5U);
    assert(!bt_link_scan_apply(&scan, &frame));
    frame = frame_of(JBT_MSG_STATUS, payload, writer.length);
    assert(!bt_link_scan_apply(&scan, &frame));
    assert(scan.count == 2U);

    char text[18];
    bt_link_address_to_text(jbl, text, sizeof(text));
    assert(strcmp(text, "3D:AB:55:FA:58:FC") == 0);
    uint8_t back[6];
    assert(bt_link_address_from_text(text, back) && memcmp(back, jbl, 6U) == 0);
    assert(bt_link_address_from_text("3d:ab:55:fa:58:fc", back) && memcmp(back, jbl, 6U) == 0);
    assert(!bt_link_address_from_text("3D:AB:55:FA:58", back));
    assert(!bt_link_address_from_text("3D:AB:55:FA:58:FC:00", back));
    assert(!bt_link_address_from_text("", back));
}

/* The phone's level, for the meter: two little-endian words, held to the
 * meter's full scale, and nothing for a frame that is not one or is short. */
static void test_a_level_frame_gives_the_meter_two_readings(void)
{
    uint8_t payload[4];
    jbt_writer_t writer;
    jbt_writer_init(&writer, payload, sizeof(payload));
    assert(jbt_put_u16(&writer, 1234U) && jbt_put_u16(&writer, 32768U));
    const jbt_frame_t frame = frame_of(JBT_MSG_LEVEL, payload, writer.length);
    uint16_t left = 0U;
    uint16_t right = 0U;
    assert(bt_link_model_level(&frame, &left, &right));
    assert(left == 1234U && right == 32768U);

    const uint8_t loud[4] = {0xFF, 0xFF, 0x01, 0x00};
    const jbt_frame_t over = frame_of(JBT_MSG_LEVEL, loud, sizeof(loud));
    assert(bt_link_model_level(&over, &left, &right));
    assert(left == 32768U && right == 1U);

    const jbt_frame_t shorter = frame_of(JBT_MSG_LEVEL, payload, 3U);
    assert(!bt_link_model_level(&shorter, &left, &right));
    const jbt_frame_t other = frame_of(JBT_MSG_POSITION, payload, 4U);
    assert(!bt_link_model_level(&other, &left, &right));
    assert(strcmp(jbt_msg_name(JBT_MSG_LEVEL), "LEVEL") == 0);
}

/* A talking module is not pinged - until its version is unknown, which is the
 * state after this board reboots while a phone plays and the module never
 * falls silent: then a PING goes out on the beat regardless, and no faster. */
static void test_a_ping_goes_out_while_the_version_is_unknown(void)
{
    const uint32_t beat = 2000U;
    const int64_t s = 1000000;
    // Heard 100 ms ago, version known: no PING however long since the last.
    assert(!bt_link_model_ping_due(10 * s, 0, 10 * s - s / 10, true, beat));
    // Silent for 3 s: PING.
    assert(bt_link_model_ping_due(10 * s, 0, 7 * s, true, beat));
    // Version unknown, module chatty: PING anyway...
    assert(bt_link_model_ping_due(10 * s, 7 * s, 10 * s - s / 10, false, beat));
    // ...but not twice inside the beat.
    assert(!bt_link_model_ping_due(10 * s, 9 * s, 10 * s - s / 10, false, beat));
}

int main(void)
{
    test_a_scan_lists_each_speaker_once_and_keeps_its_name();
    test_a_cover_is_fetched_piece_by_piece_and_only_once();
    test_a_status_frame_fills_the_snapshot();
    test_a_track_replaces_the_previous_one_whole();
    test_the_small_frames_move_one_field_each();
    test_a_log_frame_is_read_for_printing();
    test_the_volume_scales_round_trip();
    test_a_level_frame_gives_the_meter_two_readings();
    test_a_ping_goes_out_while_the_version_is_unknown();
    puts("bt_link_model tests passed");
    return 0;
}
