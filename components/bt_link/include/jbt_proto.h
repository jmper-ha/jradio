#pragma once

/* The wire protocol between jRadio (the ESP32-S3 host) and this module, over
 * one UART. Pure C, no ESP-IDF in it: the same file is compiled into the
 * host's firmware, into this module and into the host tests, so the two ends
 * cannot drift apart - and the encoder and decoder are tested against each
 * other on a PC before either board sees a byte.
 *
 * Framing is SLIP (RFC 1055): every frame ends with 0xC0, and 0xC0 / 0xDB
 * inside a frame are escaped. Chosen over a length-prefixed header because a
 * SLIP decoder resynchronises on the next 0xC0 by itself after any garbage -
 * a reset of either side mid-frame, a bit of noise on the line - while a
 * length-prefixed one has to guess where the next frame starts.
 *
 * Inside the frame:
 *
 *     type u8 | flags u8 | seq u8 | len u16 LE | payload[len] | crc16 LE
 *
 * The CRC is CRC-16/CCITT-FALSE over type..payload. `seq` counts per sender;
 * an ACK carries the seq it answers in its payload. Strings never travel bare:
 * they go in TLV records, so a message can grow a field without breaking a
 * reader that does not know it.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JBT_PROTOCOL_VERSION 1U

/* SLIP bytes. */
#define JBT_SLIP_END 0xC0U
#define JBT_SLIP_ESC 0xDBU
#define JBT_SLIP_ESC_END 0xDCU
#define JBT_SLIP_ESC_ESC 0xDDU

/* The largest payload either side sends: a cover-art chunk of 512 bytes with
 * its offset in front. Everything else is far smaller. Sized once here so
 * both ends allocate the same receive buffer. */
#define JBT_PAYLOAD_MAX 520U
#define JBT_HEADER_SIZE 5U
#define JBT_CRC_SIZE 2U
#define JBT_FRAME_MAX (JBT_HEADER_SIZE + JBT_PAYLOAD_MAX + JBT_CRC_SIZE)
/* Worst case on the wire: every byte escaped, plus the END on both sides. */
#define JBT_WIRE_MAX (2U * JBT_FRAME_MAX + 2U)

/* Header flags. */
#define JBT_FLAG_WANT_ACK 0x01U /* the sender expects JBT_MSG_ACK for this seq */
#define JBT_FLAG_IS_ACK 0x02U   /* set on ACK/NACK frames */

/* Message types. Host -> module in 0x01..0x7F, module -> host in 0x80..0xFE.
 * The split is so a log line or a trace can tell the direction at a glance. */
typedef enum {
    JBT_MSG_NONE = 0x00,
    /* host -> module */
    JBT_MSG_SET_MODE = 0x01,    /* u8 jbt_mode_t */
    JBT_MSG_SET_NAME = 0x02,    /* TLV: NAME */
    JBT_MSG_PAIRING = 0x03,     /* u8 0/1: discoverable */
    JBT_MSG_SCAN = 0x04,        /* u8 0/1 */
    JBT_MSG_CONNECT = 0x05,     /* addr[6] */
    JBT_MSG_DISCONNECT = 0x06,  /* - */
    JBT_MSG_PASSTHROUGH = 0x07, /* u8 jbt_key_t */
    JBT_MSG_SET_VOLUME = 0x08,  /* u8 0..127 */
    JBT_MSG_I2S_FORMAT = 0x09,  /* u32 rate, u8 bits, u8 channels */
    JBT_MSG_GET_STATUS = 0x0A,  /* - */
    JBT_MSG_COVER_GET = 0x0B,   /* u32 offset, u16 max */
    JBT_MSG_FORGET = 0x0C,      /* addr[6], or empty for all */
    JBT_MSG_PING = 0x0D,        /* - */
    /* module -> host */
    JBT_MSG_STATUS = 0x80,      /* jbt_status_t fields, then TLV: PEER_NAME */
    JBT_MSG_MODE_ACK = 0x81,    /* u8 mode, u8 jbt_result_t */
    JBT_MSG_TRACK = 0x82,       /* TLV: TITLE, ARTIST, ALBUM, GENRE, DURATION_MS, TRACK_NO, COVER_HASH */
    JBT_MSG_POSITION = 0x83,    /* u32 position_ms */
    JBT_MSG_PLAY_STATE = 0x84,  /* u8 jbt_play_t */
    JBT_MSG_VOLUME = 0x85,      /* u8 0..127 */
    JBT_MSG_COVER_INFO = 0x86,  /* u32 size, u8 jbt_image_t, u32 hash, u16 w, u16 h */
    JBT_MSG_COVER_DATA = 0x87,  /* u32 offset, bytes */
    JBT_MSG_SCAN_RESULT = 0x88, /* addr[6], i8 rssi, u32 class, TLV: NAME */
    JBT_MSG_EVENT = 0x89,       /* u8 jbt_event_t, TLV: TEXT */
    JBT_MSG_LOG = 0x8A,         /* u8 level, TLV: TEXT */
    JBT_MSG_PONG = 0x8B,        /* u8 protocol, u8 fw_major, u8 fw_minor, u16 fw_build */
    /* either direction */
    JBT_MSG_ACK = 0xFE,         /* u8 seq acknowledged, u8 jbt_result_t */
} jbt_msg_t;

typedef enum {
    JBT_MODE_OFF = 0,
    JBT_MODE_SINK = 1,   /* phone -> module -> I2S out (module drives the bus) */
    JBT_MODE_SOURCE = 2, /* I2S in (host drives the bus) -> module -> headphones */
} jbt_mode_t;

typedef enum {
    JBT_CONN_NONE = 0,
    JBT_CONN_CONNECTING = 1,
    JBT_CONN_CONNECTED = 2,
    JBT_CONN_PAIRING = 3,
    JBT_CONN_SCANNING = 4,
} jbt_conn_t;

typedef enum {
    JBT_PLAY_STOPPED = 0,
    JBT_PLAY_PLAYING = 1,
    JBT_PLAY_PAUSED = 2,
} jbt_play_t;

typedef enum {
    JBT_CODEC_NONE = 0,
    JBT_CODEC_SBC = 1,
    JBT_CODEC_AAC = 2,
} jbt_codec_t;

typedef enum {
    JBT_RESULT_OK = 0,
    JBT_RESULT_BAD_ARG = 1,
    JBT_RESULT_BUSY = 2,        /* not in a mode where the command applies */
    JBT_RESULT_UNSUPPORTED = 3,
    JBT_RESULT_FAILED = 4,
} jbt_result_t;

typedef enum {
    JBT_KEY_PLAY = 0,
    JBT_KEY_PAUSE = 1,
    JBT_KEY_STOP = 2,
    JBT_KEY_NEXT = 3,
    JBT_KEY_PREV = 4,
    JBT_KEY_FAST_FORWARD = 5,
    JBT_KEY_REWIND = 6,
} jbt_key_t;

typedef enum {
    JBT_IMAGE_JPEG = 0,
    JBT_IMAGE_PNG = 1,
    JBT_IMAGE_BMP = 2,
} jbt_image_t;

typedef enum {
    JBT_EVENT_CONNECTED = 0,
    JBT_EVENT_DISCONNECTED = 1,
    JBT_EVENT_PAIRING_FAILED = 2,
    JBT_EVENT_I2S_CLOCK_LOST = 3,
    JBT_EVENT_BOOTED = 4,
} jbt_event_t;

/* TLV tags, shared by every message that carries records. */
typedef enum {
    JBT_TAG_NAME = 0x01,        /* string: the module's own name */
    JBT_TAG_PEER_NAME = 0x02,   /* string */
    JBT_TAG_TITLE = 0x10,       /* string */
    JBT_TAG_ARTIST = 0x11,      /* string */
    JBT_TAG_ALBUM = 0x12,       /* string */
    JBT_TAG_GENRE = 0x13,       /* string */
    JBT_TAG_DURATION_MS = 0x14, /* u32 */
    JBT_TAG_TRACK_NO = 0x15,    /* u32 */
    JBT_TAG_COVER_HASH = 0x16,  /* u32; absent when the track has no cover */
    JBT_TAG_TEXT = 0x20,        /* string: free text of an event or a log line */
} jbt_tag_t;

/* A frame as the program sees it. `payload` points at the caller's bytes on
 * the way out and into the decoder's buffer on the way in. */
typedef struct {
    uint8_t type;
    uint8_t flags;
    uint8_t seq;
    uint16_t len;
    const uint8_t *payload;
} jbt_frame_t;

/* CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection. The check
 * value for "123456789" is 0x29B1, and the test pins that. */
uint16_t jbt_crc16(const uint8_t *data, size_t length);

/* Encodes a frame into its SLIP form, END on both sides (the leading END
 * flushes whatever noise preceded it). Returns the number of bytes written,
 * or 0 when the frame does not fit `out` or the payload exceeds
 * JBT_PAYLOAD_MAX. */
size_t jbt_frame_encode(const jbt_frame_t *frame, uint8_t *out, size_t out_size);

/* A byte-at-a-time decoder, one per link. Feed it every byte received; it
 * hands back a frame when one has closed with a good CRC. The frame's
 * payload points into the decoder and is valid until the next feed. */
typedef struct {
    uint8_t buffer[JBT_FRAME_MAX];
    size_t length;
    bool escaping;
    bool overrun;
    /* Frames dropped since init: bad CRC, too short, too long, bad escape.
     * A counter rather than a log line, because the link layer decides what
     * to do about noise and the decoder should not care. */
    uint32_t dropped;
} jbt_decoder_t;

void jbt_decoder_init(jbt_decoder_t *decoder);
bool jbt_decoder_feed(jbt_decoder_t *decoder, uint8_t byte, jbt_frame_t *out);

/* Little-endian field helpers for hand-built payloads. Each returns false and
 * writes nothing when the value would not fit; the reader variants return
 * false when the field runs past `length`. */
typedef struct {
    uint8_t *buffer;
    size_t size;
    size_t length;
    bool overflow;
} jbt_writer_t;

void jbt_writer_init(jbt_writer_t *writer, uint8_t *buffer, size_t size);
bool jbt_put_u8(jbt_writer_t *writer, uint8_t value);
bool jbt_put_u16(jbt_writer_t *writer, uint16_t value);
bool jbt_put_u32(jbt_writer_t *writer, uint32_t value);
bool jbt_put_bytes(jbt_writer_t *writer, const void *bytes, size_t length);
/* A TLV record: tag, length (one byte, so 255 max), value. A string longer
 * than 255 bytes is clipped at a UTF-8 boundary rather than refused: a title
 * that long is a title nobody reads to the end, and refusing it would drop
 * the whole message. */
bool jbt_put_tlv_string(jbt_writer_t *writer, uint8_t tag, const char *text);
bool jbt_put_tlv_u32(jbt_writer_t *writer, uint8_t tag, uint32_t value);
bool jbt_put_tlv_bytes(jbt_writer_t *writer, uint8_t tag, const void *bytes, size_t length);

typedef struct {
    const uint8_t *data;
    size_t length;
    size_t position;
} jbt_reader_t;

void jbt_reader_init(jbt_reader_t *reader, const uint8_t *data, size_t length);
bool jbt_get_u8(jbt_reader_t *reader, uint8_t *value);
bool jbt_get_u16(jbt_reader_t *reader, uint16_t *value);
bool jbt_get_u32(jbt_reader_t *reader, uint32_t *value);
bool jbt_get_bytes(jbt_reader_t *reader, void *bytes, size_t length);
/* Walks the TLV records that follow the fixed fields. False at the end or on
 * a record that runs past the payload. */
bool jbt_get_tlv(jbt_reader_t *reader, uint8_t *tag, const uint8_t **value, uint8_t *length);
/* Copies a TLV value into a NUL-terminated buffer, clipping at a UTF-8
 * boundary. */
void jbt_tlv_to_string(const uint8_t *value, uint8_t length, char *out, size_t out_size);
/* Reads a u32 TLV value; false when the record is not four bytes. */
bool jbt_tlv_to_u32(const uint8_t *value, uint8_t length, uint32_t *out);

/* The fixed part of JBT_MSG_STATUS, in wire order. `peer_name` follows as a
 * TLV. */
typedef struct {
    uint8_t mode;       /* jbt_mode_t */
    uint8_t connection; /* jbt_conn_t */
    uint8_t peer[6];    /* all zero when not connected */
    uint8_t codec;      /* jbt_codec_t */
    uint32_t sample_rate;
    uint8_t play;       /* jbt_play_t */
    uint8_t volume;     /* 0..127 */
} jbt_status_t;

#define JBT_STATUS_FIXED_SIZE 15U

bool jbt_put_status(jbt_writer_t *writer, const jbt_status_t *status);
bool jbt_get_status(jbt_reader_t *reader, jbt_status_t *status);

/* Names for logs and the PC tool, so a trace reads "STATUS" and not "0x80". */
const char *jbt_msg_name(uint8_t type);

#ifdef __cplusplus
}
#endif
