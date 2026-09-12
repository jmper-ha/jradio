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

    /* A later STATUS without a name clears it: the name belongs to the
     * connection it came with. */
    jbt_writer_init(&writer, payload, sizeof(payload));
    jbt_status_t gone = status;
    gone.connection = JBT_CONN_NONE;
    memset(gone.peer, 0, sizeof(gone.peer));
    assert(jbt_put_status(&writer, &gone));
    const jbt_frame_t frame2 = frame_of(JBT_MSG_STATUS, payload, writer.length);
    assert(bt_link_model_apply(&state, &frame2) == BT_LINK_CHANGED_STATUS);
    assert(state.peer_name[0] == '\0');

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

int main(void)
{
    test_a_cover_is_fetched_piece_by_piece_and_only_once();
    test_a_status_frame_fills_the_snapshot();
    test_a_track_replaces_the_previous_one_whole();
    test_the_small_frames_move_one_field_each();
    test_a_log_frame_is_read_for_printing();
    test_the_volume_scales_round_trip();
    puts("bt_link_model tests passed");
    return 0;
}
