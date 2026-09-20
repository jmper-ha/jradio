#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Turning what the infrared receiver saw into a key.
 *
 * A receiver's output is a train of marks (the carrier on) and spaces, in
 * microseconds. NEC - which is what most television and set-top remotes
 * speak, the Toshiba CT-893 among them - is decoded properly: a 9 ms leader,
 * 32 bits, the address and the command each followed by its complement, and
 * a shorter frame every 110 ms for as long as the key is held. Anything else
 * is not decoded at all but *recognised*: its timings are rounded onto a
 * coarse grid and hashed, and the same key on the same remote gives the same
 * hash every time - which is all a learning table needs.
 *
 * Pure so the tests can feed it frames from a datasheet and from a capture. */

typedef struct {
    uint16_t mark_us;
    uint16_t space_us;
} ir_pulse_t;

typedef enum {
    IR_CODE_NONE = 0,
    IR_CODE_NEC,
    IR_CODE_NEC_REPEAT,
    IR_CODE_RAW,
} ir_code_kind_t;

typedef struct {
    ir_code_kind_t kind;
    /* NEC: the address as sent, 8 bits - or 16 for the extended form, where
     * the second byte is not the complement of the first. */
    uint16_t address;
    uint8_t command;
    /* RAW: the hash of the pulse train. */
    uint32_t hash;
} ir_code_t;

/* The most pulses a frame is looked at with; longer trains are hashed on
 * their first IR_DECODE_MAX_PULSES, which is still a stable signature. */
#define IR_DECODE_MAX_PULSES 64U

ir_code_t ir_decode(const ir_pulse_t *pulses, size_t count);

/* "nec:40:12", "nec-repeat", "raw:9f3e21c7", "" - what the log and the
 * learning table write. */
void ir_code_format(const ir_code_t *code, char *out, size_t size);
bool ir_code_parse(const char *text, ir_code_t *code);
bool ir_code_same_key(const ir_code_t *left, const ir_code_t *right);
