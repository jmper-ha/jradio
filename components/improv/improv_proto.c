#include "improv_proto.h"

#include <string.h>

static const uint8_t k_header[6] = {'I', 'M', 'P', 'R', 'O', 'V'};

void improv_decoder_reset(improv_decoder_t *decoder)
{
    if (decoder == NULL) return;
    /* The data field may have held a password. */
    memset(decoder, 0, sizeof(*decoder));
}

/* Where a byte lands: the six header bytes, then version, type and length,
 * then the data, then the checksum. */
bool improv_decoder_feed(improv_decoder_t *decoder, uint8_t byte)
{
    if (decoder == NULL) return false;
    if (decoder->matched < sizeof(k_header)) {
        if (byte == k_header[decoder->matched]) {
            if (decoder->matched == 0U) decoder->sum = 0U;
            decoder->sum = (uint8_t)(decoder->sum + byte);
            ++decoder->matched;
        } else {
            /* A log line, or a header that broke off: resync, counting this
             * byte if it could be the start of the next one. */
            decoder->matched = 0U;
            if (byte == k_header[0]) {
                decoder->sum = byte;
                decoder->matched = 1U;
            }
        }
        return false;
    }
    const uint8_t field = (uint8_t)(decoder->matched - sizeof(k_header));
    if (field < 3U) {
        decoder->sum = (uint8_t)(decoder->sum + byte);
        if (field == 0U && byte != IMPROV_SERIAL_VERSION) {
            decoder->matched = 0U;
            return false;
        }
        if (field == 1U) decoder->type = byte;
        if (field == 2U) {
            decoder->length = byte;
            decoder->received = 0U;
        }
        ++decoder->matched;
        return false;
    }
    if (decoder->received < decoder->length) {
        decoder->data[decoder->received++] = byte;
        decoder->sum = (uint8_t)(decoder->sum + byte);
        return false;
    }
    // The checksum.
    const bool good = byte == decoder->sum;
    decoder->matched = 0U;
    return good;
}

improv_error_t improv_rpc_parse(const uint8_t *data, size_t length, improv_rpc_t *rpc)
{
    if (data == NULL || rpc == NULL || length < 2U) return IMPROV_ERROR_INVALID_RPC;
    memset(rpc, 0, sizeof(*rpc));
    rpc->command = data[0];
    const size_t payload_length = data[1];
    if (2U + payload_length > length) return IMPROV_ERROR_INVALID_RPC;
    const uint8_t *payload = data + 2;
    switch (rpc->command) {
    case IMPROV_COMMAND_WIFI_SETTINGS: {
        if (payload_length < 1U) return IMPROV_ERROR_INVALID_RPC;
        const size_t ssid_length = payload[0];
        if (ssid_length == 0U || ssid_length > IMPROV_SSID_MAX ||
            1U + ssid_length + 1U > payload_length) {
            return IMPROV_ERROR_INVALID_RPC;
        }
        const size_t password_length = payload[1U + ssid_length];
        if (password_length > IMPROV_PASSWORD_MAX ||
            1U + ssid_length + 1U + password_length > payload_length) {
            return IMPROV_ERROR_INVALID_RPC;
        }
        memcpy(rpc->ssid, payload + 1, ssid_length);
        memcpy(rpc->password, payload + 2U + ssid_length, password_length);
        /* A NUL inside either would be a different network from the one the
         * user typed once it reached the C strings below. */
        if (strlen(rpc->ssid) != ssid_length || strlen(rpc->password) != password_length) {
            memset(rpc, 0, sizeof(*rpc));
            return IMPROV_ERROR_INVALID_RPC;
        }
        return IMPROV_ERROR_NONE;
    }
    case IMPROV_COMMAND_GET_STATE:
    case IMPROV_COMMAND_GET_INFO:
    case IMPROV_COMMAND_GET_NETWORKS:
        return IMPROV_ERROR_NONE;
    default:
        return IMPROV_ERROR_UNKNOWN_RPC;
    }
}

size_t improv_frame(uint8_t type, const uint8_t *data, size_t length, uint8_t *out,
                    size_t capacity)
{
    if (out == NULL || length > IMPROV_DATA_MAX || (length > 0U && data == NULL)) return 0U;
    const size_t total = sizeof(k_header) + 3U + length + 2U;
    if (total > capacity) return 0U;
    size_t at = 0U;
    memcpy(out, k_header, sizeof(k_header));
    at += sizeof(k_header);
    out[at++] = IMPROV_SERIAL_VERSION;
    out[at++] = type;
    out[at++] = (uint8_t)length;
    if (length > 0U) memcpy(out + at, data, length);
    at += length;
    uint8_t sum = 0U;
    for (size_t index = 0U; index < at; ++index) sum = (uint8_t)(sum + out[index]);
    out[at++] = sum;
    out[at++] = '\n';
    return at;
}

size_t improv_frame_state(improv_state_t state, uint8_t *out, size_t capacity)
{
    const uint8_t data = (uint8_t)state;
    return improv_frame(IMPROV_TYPE_CURRENT_STATE, &data, 1U, out, capacity);
}

size_t improv_frame_error(improv_error_t error, uint8_t *out, size_t capacity)
{
    const uint8_t data = (uint8_t)error;
    return improv_frame(IMPROV_TYPE_ERROR_STATE, &data, 1U, out, capacity);
}

size_t improv_frame_result(improv_command_t command, const char *const *strings, size_t count,
                           uint8_t *out, size_t capacity)
{
    uint8_t data[IMPROV_DATA_MAX];
    size_t at = 2U;
    for (size_t index = 0U; index < count; ++index) {
        const char *text = strings[index] != NULL ? strings[index] : "";
        const size_t length = strlen(text);
        if (length > 255U || at + 1U + length > sizeof(data)) return 0U;
        data[at++] = (uint8_t)length;
        memcpy(data + at, text, length);
        at += length;
    }
    data[0] = (uint8_t)command;
    data[1] = (uint8_t)(at - 2U);
    return improv_frame(IMPROV_TYPE_RPC_RESULT, data, at, out, capacity);
}
