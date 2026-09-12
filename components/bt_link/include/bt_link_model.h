#pragma once

/* What the host knows about the Bluetooth module, and how a frame from it
 * changes that. Pure: the link feeds frames in, the snapshot is read out,
 * and the tests drive both without a UART.
 *
 * jbt_proto.h here is a copy of the module's, byte for byte; the host test
 * suite checks the two are identical when the module's repository sits
 * beside this one. */

#include <stdbool.h>
#include <stdint.h>

#include "jbt_proto.h"

#define BT_LINK_TEXT_MAX 128U
#define BT_LINK_NAME_MAX 64U

typedef struct {
    /* From PONG. */
    uint8_t protocol;
    uint8_t fw_major;
    uint8_t fw_minor;
    uint16_t fw_build;
    /* From STATUS. */
    jbt_status_t status;
    char peer_name[BT_LINK_NAME_MAX];
    /* From TRACK; the revision moves with every TRACK so a reader that
     * copies these can tell a new track from the same one again. */
    char title[BT_LINK_TEXT_MAX];
    char artist[BT_LINK_TEXT_MAX];
    char album[BT_LINK_TEXT_MAX];
    uint32_t duration_ms;
    uint32_t track_no;
    uint32_t track_revision;
    /* From POSITION. */
    uint32_t position_ms;
    /* From the last MODE_ACK, and how many there have been - the waiter
     * compares counts rather than trusting a flag it might have cleared. */
    uint8_t acked_mode;
    uint8_t acked_result;
    uint32_t mode_acks;
    /* From the last EVENT. */
    uint8_t event;
    uint32_t events;
    /* The cover: what the module announced and how far the fetch is. The
     * fetch is one request in flight at a time - COVER_GET, COVER_DATA, the
     * next - and the model only says what to ask for; the bytes go into a
     * buffer the link owns. */
    uint32_t cover_size;      /* 0: the track has no cover */
    uint8_t cover_kind;       /* jbt_image_t */
    uint32_t cover_hash;      /* what the module holds */
    uint32_t cover_received;  /* bytes fetched so far, contiguous from 0 */
    uint32_t cover_done_hash; /* the hash of the last cover fully fetched */
    uint32_t cover_revision;  /* moves with every COVER_INFO */
} bt_link_state_t;

/* The piece of a cover to ask for at most: a frame's payload less the
 * offset in front. */
#define BT_LINK_COVER_CHUNK (JBT_PAYLOAD_MAX - 4U)
/* The most cover the host takes; the module keeps 48 KB, so this is only a
 * guard against a module that lies. */
#define BT_LINK_COVER_MAX (64U * 1024U)

/* What a frame changed, as bits, so the link can wake the right waiter and
 * the UI can redraw only what moved. */
#define BT_LINK_CHANGED_PONG 0x01U
#define BT_LINK_CHANGED_STATUS 0x02U
#define BT_LINK_CHANGED_TRACK 0x04U
#define BT_LINK_CHANGED_POSITION 0x08U
#define BT_LINK_CHANGED_PLAY 0x10U
#define BT_LINK_CHANGED_VOLUME 0x20U
#define BT_LINK_CHANGED_MODE_ACK 0x40U
#define BT_LINK_CHANGED_EVENT 0x80U
#define BT_LINK_CHANGED_COVER_INFO 0x100U
#define BT_LINK_CHANGED_COVER_DATA 0x200U

void bt_link_model_init(bt_link_state_t *state);

/* Applies one frame; returns the change bits (0 for a frame the model does
 * not keep - a LOG, an ACK, a cover chunk). A malformed payload changes
 * nothing. */
uint32_t bt_link_model_apply(bt_link_state_t *state, const jbt_frame_t *frame);

/* Whether a piece of the cover is wanted, and which: true with the offset
 * and length to ask for while a cover is announced, unfetched, and would
 * fit; false when there is nothing to fetch or the fetch is complete. */
bool bt_link_model_cover_wanted(const bt_link_state_t *state, uint32_t *offset, uint16_t *length);

/* A COVER_DATA frame's place and bytes, checked against the fetch: false
 * when it is not the piece expected (a stale answer, a frame for a cover
 * since replaced), in which case the fetch is unchanged. On true the
 * caller copies `bytes` to `offset` in its buffer; the model has already
 * counted them. `complete` says the whole cover is now in. */
bool bt_link_model_cover_data(bt_link_state_t *state, const jbt_frame_t *frame, uint32_t *offset,
                              const uint8_t **bytes, size_t *length, bool *complete);

/* A LOG frame's level and text. False for any other frame. */
bool bt_link_model_log(const jbt_frame_t *frame, uint8_t *level, char *text, size_t text_size);

/* The 0..100 volume the board keeps and the 0..127 the module speaks,
 * each way, rounded so a value survives the round trip. */
uint8_t bt_link_volume_to_module(uint8_t percent);
uint8_t bt_link_volume_to_percent(uint8_t module);
