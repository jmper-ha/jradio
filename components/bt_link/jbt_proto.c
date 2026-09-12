#include "jbt_proto.h"

#include <string.h>

uint16_t jbt_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFU;
    for (size_t i = 0; i < length; ++i) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000U) ? (uint16_t)((crc << 1) ^ 0x1021U) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* Writes one byte in SLIP form; returns the number of wire bytes, 0 when
 * `out` is full. Two escapes are the whole of SLIP, which is the point. */
static size_t jbt_slip_put(uint8_t byte, uint8_t *out, size_t room)
{
    if (byte == JBT_SLIP_END) {
        if (room < 2U) return 0U;
        out[0] = JBT_SLIP_ESC;
        out[1] = JBT_SLIP_ESC_END;
        return 2U;
    }
    if (byte == JBT_SLIP_ESC) {
        if (room < 2U) return 0U;
        out[0] = JBT_SLIP_ESC;
        out[1] = JBT_SLIP_ESC_ESC;
        return 2U;
    }
    if (room < 1U) return 0U;
    out[0] = byte;
    return 1U;
}

size_t jbt_frame_encode(const jbt_frame_t *frame, uint8_t *out, size_t out_size)
{
    if (frame == NULL || out == NULL) return 0U;
    if (frame->len > JBT_PAYLOAD_MAX) return 0U;
    if (frame->len > 0U && frame->payload == NULL) return 0U;

    /* The raw frame first, so the CRC runs over exactly what the decoder
     * will see after unescaping. */
    uint8_t raw[JBT_FRAME_MAX];
    raw[0] = frame->type;
    raw[1] = frame->flags;
    raw[2] = frame->seq;
    raw[3] = (uint8_t)(frame->len & 0xFFU);
    raw[4] = (uint8_t)(frame->len >> 8);
    if (frame->len > 0U) memcpy(&raw[JBT_HEADER_SIZE], frame->payload, frame->len);
    const size_t body = JBT_HEADER_SIZE + frame->len;
    const uint16_t crc = jbt_crc16(raw, body);
    raw[body] = (uint8_t)(crc & 0xFFU);
    raw[body + 1U] = (uint8_t)(crc >> 8);
    const size_t raw_length = body + JBT_CRC_SIZE;

    size_t written = 0U;
    if (out_size < 1U) return 0U;
    out[written++] = JBT_SLIP_END;
    for (size_t i = 0; i < raw_length; ++i) {
        const size_t n = jbt_slip_put(raw[i], &out[written], out_size - written);
        if (n == 0U) return 0U;
        written += n;
    }
    if (written >= out_size) return 0U;
    out[written++] = JBT_SLIP_END;
    return written;
}

void jbt_decoder_init(jbt_decoder_t *decoder)
{
    if (decoder == NULL) return;
    memset(decoder, 0, sizeof(*decoder));
}

/* Called on END: decides whether what accumulated is a frame. */
static bool jbt_decoder_close(jbt_decoder_t *decoder, jbt_frame_t *out)
{
    const size_t length = decoder->length;
    const bool overrun = decoder->overrun;
    const bool escaping = decoder->escaping;
    decoder->length = 0U;
    decoder->overrun = false;
    decoder->escaping = false;

    /* Back-to-back ENDs - the leading one of every frame - carry nothing and
     * are not an error. */
    if (length == 0U && !overrun && !escaping) return false;
    if (overrun || escaping || length < JBT_HEADER_SIZE + JBT_CRC_SIZE) {
        ++decoder->dropped;
        return false;
    }
    const uint16_t len = (uint16_t)(decoder->buffer[3] | ((uint16_t)decoder->buffer[4] << 8));
    if (len > JBT_PAYLOAD_MAX || JBT_HEADER_SIZE + (size_t)len + JBT_CRC_SIZE != length) {
        ++decoder->dropped;
        return false;
    }
    const size_t body = JBT_HEADER_SIZE + len;
    const uint16_t wire = (uint16_t)(decoder->buffer[body] | ((uint16_t)decoder->buffer[body + 1U] << 8));
    if (wire != jbt_crc16(decoder->buffer, body)) {
        ++decoder->dropped;
        return false;
    }
    if (out != NULL) {
        out->type = decoder->buffer[0];
        out->flags = decoder->buffer[1];
        out->seq = decoder->buffer[2];
        out->len = len;
        out->payload = &decoder->buffer[JBT_HEADER_SIZE];
    }
    return true;
}

bool jbt_decoder_feed(jbt_decoder_t *decoder, uint8_t byte, jbt_frame_t *out)
{
    if (decoder == NULL) return false;
    if (byte == JBT_SLIP_END) return jbt_decoder_close(decoder, out);

    if (decoder->escaping) {
        decoder->escaping = false;
        if (byte == JBT_SLIP_ESC_END) {
            byte = JBT_SLIP_END;
        } else if (byte == JBT_SLIP_ESC_ESC) {
            byte = JBT_SLIP_ESC;
        } else {
            /* An escape followed by anything else is not SLIP; the frame is
             * noise up to the next END. */
            decoder->overrun = true;
            return false;
        }
    } else if (byte == JBT_SLIP_ESC) {
        decoder->escaping = true;
        return false;
    }

    if (decoder->overrun) return false;
    if (decoder->length >= sizeof(decoder->buffer)) {
        /* Longer than any frame can be: keep discarding until END, and count
         * it once there rather than once per byte. */
        decoder->overrun = true;
        return false;
    }
    decoder->buffer[decoder->length++] = byte;
    return false;
}

void jbt_writer_init(jbt_writer_t *writer, uint8_t *buffer, size_t size)
{
    writer->buffer = buffer;
    writer->size = size;
    writer->length = 0U;
    writer->overflow = false;
}

bool jbt_put_bytes(jbt_writer_t *writer, const void *bytes, size_t length)
{
    if (writer->overflow || writer->length + length > writer->size) {
        writer->overflow = true;
        return false;
    }
    if (length > 0U) memcpy(&writer->buffer[writer->length], bytes, length);
    writer->length += length;
    return true;
}

bool jbt_put_u8(jbt_writer_t *writer, uint8_t value)
{
    return jbt_put_bytes(writer, &value, 1U);
}

bool jbt_put_u16(jbt_writer_t *writer, uint16_t value)
{
    const uint8_t bytes[2] = {(uint8_t)(value & 0xFFU), (uint8_t)(value >> 8)};
    return jbt_put_bytes(writer, bytes, sizeof(bytes));
}

bool jbt_put_u32(jbt_writer_t *writer, uint32_t value)
{
    const uint8_t bytes[4] = {(uint8_t)(value & 0xFFU), (uint8_t)((value >> 8) & 0xFFU),
                              (uint8_t)((value >> 16) & 0xFFU), (uint8_t)(value >> 24)};
    return jbt_put_bytes(writer, bytes, sizeof(bytes));
}

/* The longest prefix of `text` that fits `limit` bytes without cutting a
 * UTF-8 sequence: a continuation byte is 10xxxxxx, so back up over those. */
static size_t jbt_utf8_clip(const char *text, size_t limit)
{
    size_t length = strlen(text);
    if (length <= limit) return length;
    length = limit;
    while (length > 0U && ((uint8_t)text[length] & 0xC0U) == 0x80U) --length;
    return length;
}

bool jbt_put_tlv_bytes(jbt_writer_t *writer, uint8_t tag, const void *bytes, size_t length)
{
    if (length > 255U) {
        writer->overflow = true;
        return false;
    }
    return jbt_put_u8(writer, tag) && jbt_put_u8(writer, (uint8_t)length) &&
           jbt_put_bytes(writer, bytes, length);
}

bool jbt_put_tlv_string(jbt_writer_t *writer, uint8_t tag, const char *text)
{
    if (text == NULL) text = "";
    return jbt_put_tlv_bytes(writer, tag, text, jbt_utf8_clip(text, 255U));
}

bool jbt_put_tlv_u32(jbt_writer_t *writer, uint8_t tag, uint32_t value)
{
    const uint8_t bytes[4] = {(uint8_t)(value & 0xFFU), (uint8_t)((value >> 8) & 0xFFU),
                              (uint8_t)((value >> 16) & 0xFFU), (uint8_t)(value >> 24)};
    return jbt_put_tlv_bytes(writer, tag, bytes, sizeof(bytes));
}

void jbt_reader_init(jbt_reader_t *reader, const uint8_t *data, size_t length)
{
    reader->data = data;
    reader->length = length;
    reader->position = 0U;
}

bool jbt_get_bytes(jbt_reader_t *reader, void *bytes, size_t length)
{
    if (reader->position + length > reader->length) return false;
    if (length > 0U) memcpy(bytes, &reader->data[reader->position], length);
    reader->position += length;
    return true;
}

bool jbt_get_u8(jbt_reader_t *reader, uint8_t *value)
{
    return jbt_get_bytes(reader, value, 1U);
}

bool jbt_get_u16(jbt_reader_t *reader, uint16_t *value)
{
    uint8_t bytes[2];
    if (!jbt_get_bytes(reader, bytes, sizeof(bytes))) return false;
    *value = (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
    return true;
}

bool jbt_get_u32(jbt_reader_t *reader, uint32_t *value)
{
    uint8_t bytes[4];
    if (!jbt_get_bytes(reader, bytes, sizeof(bytes))) return false;
    *value = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) |
             ((uint32_t)bytes[3] << 24);
    return true;
}

bool jbt_get_tlv(jbt_reader_t *reader, uint8_t *tag, const uint8_t **value, uint8_t *length)
{
    uint8_t t;
    uint8_t l;
    if (!jbt_get_u8(reader, &t) || !jbt_get_u8(reader, &l)) return false;
    if (reader->position + l > reader->length) return false;
    *tag = t;
    *length = l;
    *value = &reader->data[reader->position];
    reader->position += l;
    return true;
}

void jbt_tlv_to_string(const uint8_t *value, uint8_t length, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0U) return;
    size_t n = length;
    if (n >= out_size) {
        n = out_size - 1U;
        while (n > 0U && (value[n] & 0xC0U) == 0x80U) --n;
    }
    if (n > 0U) memcpy(out, value, n);
    out[n] = '\0';
}

bool jbt_tlv_to_u32(const uint8_t *value, uint8_t length, uint32_t *out)
{
    if (length != 4U) return false;
    *out = (uint32_t)value[0] | ((uint32_t)value[1] << 8) | ((uint32_t)value[2] << 16) |
           ((uint32_t)value[3] << 24);
    return true;
}

bool jbt_put_status(jbt_writer_t *writer, const jbt_status_t *status)
{
    return jbt_put_u8(writer, status->mode) && jbt_put_u8(writer, status->connection) &&
           jbt_put_bytes(writer, status->peer, sizeof(status->peer)) &&
           jbt_put_u8(writer, status->codec) && jbt_put_u32(writer, status->sample_rate) &&
           jbt_put_u8(writer, status->play) && jbt_put_u8(writer, status->volume);
}

bool jbt_get_status(jbt_reader_t *reader, jbt_status_t *status)
{
    return jbt_get_u8(reader, &status->mode) && jbt_get_u8(reader, &status->connection) &&
           jbt_get_bytes(reader, status->peer, sizeof(status->peer)) &&
           jbt_get_u8(reader, &status->codec) && jbt_get_u32(reader, &status->sample_rate) &&
           jbt_get_u8(reader, &status->play) && jbt_get_u8(reader, &status->volume);
}

const char *jbt_msg_name(uint8_t type)
{
    switch ((jbt_msg_t)type) {
    case JBT_MSG_SET_MODE: return "SET_MODE";
    case JBT_MSG_SET_NAME: return "SET_NAME";
    case JBT_MSG_PAIRING: return "PAIRING";
    case JBT_MSG_SCAN: return "SCAN";
    case JBT_MSG_CONNECT: return "CONNECT";
    case JBT_MSG_DISCONNECT: return "DISCONNECT";
    case JBT_MSG_PASSTHROUGH: return "PASSTHROUGH";
    case JBT_MSG_SET_VOLUME: return "SET_VOLUME";
    case JBT_MSG_I2S_FORMAT: return "I2S_FORMAT";
    case JBT_MSG_GET_STATUS: return "GET_STATUS";
    case JBT_MSG_COVER_GET: return "COVER_GET";
    case JBT_MSG_FORGET: return "FORGET";
    case JBT_MSG_PING: return "PING";
    case JBT_MSG_STATUS: return "STATUS";
    case JBT_MSG_MODE_ACK: return "MODE_ACK";
    case JBT_MSG_TRACK: return "TRACK";
    case JBT_MSG_POSITION: return "POSITION";
    case JBT_MSG_PLAY_STATE: return "PLAY_STATE";
    case JBT_MSG_VOLUME: return "VOLUME";
    case JBT_MSG_COVER_INFO: return "COVER_INFO";
    case JBT_MSG_COVER_DATA: return "COVER_DATA";
    case JBT_MSG_SCAN_RESULT: return "SCAN_RESULT";
    case JBT_MSG_EVENT: return "EVENT";
    case JBT_MSG_LOG: return "LOG";
    case JBT_MSG_PONG: return "PONG";
    case JBT_MSG_ACK: return "ACK";
    case JBT_MSG_NONE: break;
    }
    return "?";
}
