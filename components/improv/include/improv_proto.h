#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Improv Wi-Fi over a serial line, version 1 - the open protocol the web
 * flasher uses to hand the device a network without the setup access point.
 * https://www.improv-wifi.com/serial/
 *
 * A packet is "IMPROV", the version, a type, a length, that many bytes of
 * data and a checksum - the low byte of the sum of everything before it -
 * and the device ends each one it sends with a newline so a terminal shows
 * it on a line of its own. It shares the line with the log, so the decoder
 * skips anything that is not a packet and starts again on a bad one.
 *
 * Pure: no ESP-IDF here, the host tests run it. */

#define IMPROV_SERIAL_VERSION 1U
#define IMPROV_DATA_MAX 255U
/* The longest frame this side builds: the header, a full data field, the
 * checksum and the newline. */
#define IMPROV_FRAME_MAX (6U + 3U + IMPROV_DATA_MAX + 2U)

typedef enum {
    IMPROV_TYPE_CURRENT_STATE = 0x01,
    IMPROV_TYPE_ERROR_STATE = 0x02,
    IMPROV_TYPE_RPC = 0x03,
    IMPROV_TYPE_RPC_RESULT = 0x04,
} improv_type_t;

typedef enum {
    IMPROV_STATE_READY = 0x02,
    IMPROV_STATE_PROVISIONING = 0x03,
    IMPROV_STATE_PROVISIONED = 0x04,
} improv_state_t;

typedef enum {
    IMPROV_ERROR_NONE = 0x00,
    IMPROV_ERROR_INVALID_RPC = 0x01,
    IMPROV_ERROR_UNKNOWN_RPC = 0x02,
    IMPROV_ERROR_UNABLE_TO_CONNECT = 0x03,
    IMPROV_ERROR_UNKNOWN = 0xFF,
} improv_error_t;

typedef enum {
    IMPROV_COMMAND_WIFI_SETTINGS = 0x01,
    IMPROV_COMMAND_GET_STATE = 0x02,
    IMPROV_COMMAND_GET_INFO = 0x03,
    IMPROV_COMMAND_GET_NETWORKS = 0x04,
} improv_command_t;

typedef struct {
    uint8_t matched;   /* how much of the header has been seen */
    uint8_t type;
    uint8_t length;
    uint16_t received; /* data bytes so far */
    uint8_t sum;
    uint8_t data[IMPROV_DATA_MAX];
} improv_decoder_t;

void improv_decoder_reset(improv_decoder_t *decoder);
/* True when `byte` completes a packet with a good checksum; its type, length
 * and data are then in the decoder until the next byte is fed. A bad checksum
 * or another version is dropped without a word - the client asks again - and
 * the next "I" starts over. */
bool improv_decoder_feed(improv_decoder_t *decoder, uint8_t byte);

/* The longest SSID and password Wi-Fi allows, as the RPC may carry them. */
#define IMPROV_SSID_MAX 32U
#define IMPROV_PASSWORD_MAX 64U

typedef struct {
    uint8_t command;
    char ssid[IMPROV_SSID_MAX + 1U];
    char password[IMPROV_PASSWORD_MAX + 1U];
} improv_rpc_t;

/* An RPC packet's data: the command, its length, its payload. The Wi-Fi
 * command's payload is the SSID and the password, each behind its length.
 * IMPROV_ERROR_NONE when it parsed; INVALID_RPC for one that does not add up;
 * UNKNOWN_RPC for a command this side does not have. Leaves the password in
 * `rpc` for the caller to wipe. */
improv_error_t improv_rpc_parse(const uint8_t *data, size_t length, improv_rpc_t *rpc);

/* A whole frame into `out`: 0 when it does not fit. */
size_t improv_frame(uint8_t type, const uint8_t *data, size_t length, uint8_t *out,
                    size_t capacity);
size_t improv_frame_state(improv_state_t state, uint8_t *out, size_t capacity);
size_t improv_frame_error(improv_error_t error, uint8_t *out, size_t capacity);
/* An RPC result: the command it answers and `count` strings, each behind its
 * length. No strings is the empty result that ends a list of networks. 0 when
 * a string is longer than 255 or the whole is longer than one packet. */
size_t improv_frame_result(improv_command_t command, const char *const *strings, size_t count,
                           uint8_t *out, size_t capacity);
