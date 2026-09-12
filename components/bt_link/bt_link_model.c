#include "bt_link_model.h"

#include <stddef.h>
#include <string.h>

void bt_link_model_init(bt_link_state_t *state)
{
    memset(state, 0, sizeof(*state));
    state->status.volume = 100;
}

static void bt_link_copy_tlv_string(const uint8_t *value, uint8_t length, char *out, size_t size)
{
    jbt_tlv_to_string(value, length, out, size);
}

static uint32_t bt_link_apply_status(bt_link_state_t *state, const jbt_frame_t *frame)
{
    jbt_reader_t reader;
    jbt_reader_init(&reader, frame->payload, frame->len);
    jbt_status_t status;
    if (!jbt_get_status(&reader, &status)) return 0U;
    state->status = status;
    state->peer_name[0] = '\0';
    uint8_t tag;
    const uint8_t *value;
    uint8_t length;
    while (jbt_get_tlv(&reader, &tag, &value, &length)) {
        if (tag == JBT_TAG_PEER_NAME) {
            bt_link_copy_tlv_string(value, length, state->peer_name, sizeof(state->peer_name));
        }
    }
    return BT_LINK_CHANGED_STATUS;
}

static uint32_t bt_link_apply_track(bt_link_state_t *state, const jbt_frame_t *frame)
{
    jbt_reader_t reader;
    jbt_reader_init(&reader, frame->payload, frame->len);
    /* Everything of the previous track goes: a phone that names no album
     * for this one must not leave the last one's standing. */
    state->title[0] = '\0';
    state->artist[0] = '\0';
    state->album[0] = '\0';
    state->duration_ms = 0U;
    state->track_no = 0U;
    uint8_t tag;
    const uint8_t *value;
    uint8_t length;
    while (jbt_get_tlv(&reader, &tag, &value, &length)) {
        switch (tag) {
        case JBT_TAG_TITLE: bt_link_copy_tlv_string(value, length, state->title, sizeof(state->title)); break;
        case JBT_TAG_ARTIST: bt_link_copy_tlv_string(value, length, state->artist, sizeof(state->artist)); break;
        case JBT_TAG_ALBUM: bt_link_copy_tlv_string(value, length, state->album, sizeof(state->album)); break;
        case JBT_TAG_DURATION_MS: (void)jbt_tlv_to_u32(value, length, &state->duration_ms); break;
        case JBT_TAG_TRACK_NO: (void)jbt_tlv_to_u32(value, length, &state->track_no); break;
        default: break; /* a tag this build does not know: the point of TLV */
        }
    }
    ++state->track_revision;
    /* A new track starts at its beginning until the module says otherwise. */
    state->position_ms = 0U;
    return BT_LINK_CHANGED_TRACK;
}

uint32_t bt_link_model_apply(bt_link_state_t *state, const jbt_frame_t *frame)
{
    if (state == NULL || frame == NULL) return 0U;
    jbt_reader_t reader;
    jbt_reader_init(&reader, frame->payload, frame->len);
    switch ((jbt_msg_t)frame->type) {
    case JBT_MSG_PONG: {
        uint8_t protocol;
        uint8_t major;
        uint8_t minor;
        uint16_t build;
        if (!jbt_get_u8(&reader, &protocol) || !jbt_get_u8(&reader, &major) ||
            !jbt_get_u8(&reader, &minor) || !jbt_get_u16(&reader, &build)) {
            return 0U;
        }
        state->protocol = protocol;
        state->fw_major = major;
        state->fw_minor = minor;
        state->fw_build = build;
        return BT_LINK_CHANGED_PONG;
    }
    case JBT_MSG_STATUS: return bt_link_apply_status(state, frame);
    case JBT_MSG_TRACK: return bt_link_apply_track(state, frame);
    case JBT_MSG_POSITION: {
        uint32_t position;
        if (!jbt_get_u32(&reader, &position)) return 0U;
        state->position_ms = position;
        return BT_LINK_CHANGED_POSITION;
    }
    case JBT_MSG_PLAY_STATE: {
        uint8_t play;
        if (!jbt_get_u8(&reader, &play) || play > JBT_PLAY_PAUSED) return 0U;
        state->status.play = play;
        return BT_LINK_CHANGED_PLAY;
    }
    case JBT_MSG_VOLUME: {
        uint8_t volume;
        if (!jbt_get_u8(&reader, &volume)) return 0U;
        state->status.volume = volume > 127U ? 127U : volume;
        return BT_LINK_CHANGED_VOLUME;
    }
    case JBT_MSG_MODE_ACK: {
        uint8_t mode;
        uint8_t result;
        if (!jbt_get_u8(&reader, &mode) || !jbt_get_u8(&reader, &result)) return 0U;
        state->acked_mode = mode;
        state->acked_result = result;
        ++state->mode_acks;
        /* The ack names the mode the module is in now, which is the truth
         * about the bus whatever the last STATUS said. */
        state->status.mode = mode;
        return BT_LINK_CHANGED_MODE_ACK;
    }
    case JBT_MSG_COVER_INFO: {
        uint32_t size;
        uint8_t kind;
        uint32_t hash;
        if (!jbt_get_u32(&reader, &size) || !jbt_get_u8(&reader, &kind) || !jbt_get_u32(&reader, &hash)) {
            return 0U;
        }
        state->cover_size = size;
        state->cover_kind = kind;
        state->cover_hash = hash;
        state->cover_received = 0U;
        ++state->cover_revision;
        return BT_LINK_CHANGED_COVER_INFO;
    }
    case JBT_MSG_EVENT: {
        uint8_t event;
        if (!jbt_get_u8(&reader, &event)) return 0U;
        state->event = event;
        ++state->events;
        return BT_LINK_CHANGED_EVENT;
    }
    default: return 0U;
    }
}

bool bt_link_model_cover_wanted(const bt_link_state_t *state, uint32_t *offset, uint16_t *length)
{
    if (state->cover_size == 0U || state->cover_size > BT_LINK_COVER_MAX) return false;
    if (state->cover_hash == state->cover_done_hash) return false;
    if (state->cover_received >= state->cover_size) return false;
    const uint32_t left = state->cover_size - state->cover_received;
    *offset = state->cover_received;
    *length = (uint16_t)(left < BT_LINK_COVER_CHUNK ? left : BT_LINK_COVER_CHUNK);
    return true;
}

bool bt_link_model_cover_data(bt_link_state_t *state, const jbt_frame_t *frame, uint32_t *offset,
                              const uint8_t **bytes, size_t *length, bool *complete)
{
    if (frame->type != JBT_MSG_COVER_DATA || frame->len < 4U) return false;
    jbt_reader_t reader;
    jbt_reader_init(&reader, frame->payload, frame->len);
    uint32_t at = 0U;
    (void)jbt_get_u32(&reader, &at);
    const size_t n = frame->len - 4U;
    /* Only the piece in flight counts: an answer to an earlier request, or
     * one that arrives after COVER_INFO moved the goal, is not this cover. */
    if (at != state->cover_received || n == 0U || at + n > state->cover_size) return false;
    state->cover_received += (uint32_t)n;
    *offset = at;
    *bytes = &frame->payload[4];
    *length = n;
    *complete = state->cover_received == state->cover_size;
    if (*complete) state->cover_done_hash = state->cover_hash;
    return true;
}

bool bt_link_model_log(const jbt_frame_t *frame, uint8_t *level, char *text, size_t text_size)
{
    if (frame == NULL || frame->type != JBT_MSG_LOG) return false;
    jbt_reader_t reader;
    jbt_reader_init(&reader, frame->payload, frame->len);
    uint8_t lvl;
    if (!jbt_get_u8(&reader, &lvl)) return false;
    if (level != NULL) *level = lvl;
    if (text != NULL && text_size > 0U) text[0] = '\0';
    uint8_t tag;
    const uint8_t *value;
    uint8_t length;
    while (jbt_get_tlv(&reader, &tag, &value, &length)) {
        if (tag == JBT_TAG_TEXT && text != NULL) jbt_tlv_to_string(value, length, text, text_size);
    }
    return true;
}

uint8_t bt_link_volume_to_module(uint8_t percent)
{
    if (percent > 100U) percent = 100U;
    return (uint8_t)((percent * 127U + 50U) / 100U);
}

uint8_t bt_link_volume_to_percent(uint8_t module)
{
    if (module > 127U) module = 127U;
    return (uint8_t)((module * 100U + 63U) / 127U);
}
