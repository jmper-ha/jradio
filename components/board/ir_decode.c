#include "ir_decode.h"

#include <stdio.h>
#include <string.h>

/* NEC's nominal timings, and how far a measured one may stray. Receivers
 * stretch marks by tens of microseconds and remotes drift with their
 * batteries, so the windows are wide: what has to be told apart is 562 from
 * 1687, and those are three to one. */
#define NEC_LEADER_MARK_US 9000U
#define NEC_LEADER_SPACE_US 4500U
#define NEC_REPEAT_SPACE_US 2250U
#define NEC_BIT_MARK_US 562U
#define NEC_ZERO_SPACE_US 562U
#define NEC_ONE_SPACE_US 1687U
#define NEC_BITS 32U

static bool near(uint32_t measured, uint32_t nominal, uint32_t tolerance)
{
    return measured + tolerance >= nominal && measured <= nominal + tolerance;
}

static bool nec_bit_mark(uint16_t mark_us)
{
    return near(mark_us, NEC_BIT_MARK_US, 300U);
}

/* The 32 bits after the leader, least significant first as NEC sends them.
 * The last pulse's space is the gap to the next frame and carries no bit. */
static bool nec_decode_bits(const ir_pulse_t *pulses, size_t count, uint32_t *bits)
{
    if (count < 1U + NEC_BITS) return false;
    uint32_t value = 0U;
    for (size_t index = 0U; index < NEC_BITS; ++index) {
        const ir_pulse_t pulse = pulses[1U + index];
        if (!nec_bit_mark(pulse.mark_us)) return false;
        /* The very last bit's space is the inter-frame gap on some receivers
         * and a proper space on others; only a mark that follows makes it a
         * space, so the last bit is judged by its mark alone. */
        const bool last = index == NEC_BITS - 1U;
        bool one;
        if (near(pulse.space_us, NEC_ONE_SPACE_US, 500U)) one = true;
        else if (near(pulse.space_us, NEC_ZERO_SPACE_US, 300U)) one = false;
        else if (last) one = pulse.space_us > 1100U;
        else return false;
        if (one) value |= 1UL << index;
    }
    *bits = value;
    return true;
}

static uint32_t fnv1a(const uint8_t *bytes, size_t length)
{
    uint32_t hash = 2166136261U;
    for (size_t index = 0U; index < length; ++index) {
        hash ^= bytes[index];
        hash *= 16777619U;
    }
    return hash;
}

/* A signature that survives the jitter of a real receiver: every duration
 * is rounded to the nearest 200 us before it is hashed, and the pulse count
 * goes in first so a train cut short reads as a different key rather than a
 * prefix of the same one. */
static uint32_t raw_hash(const ir_pulse_t *pulses, size_t count)
{
    if (count > IR_DECODE_MAX_PULSES) count = IR_DECODE_MAX_PULSES;
    uint8_t bytes[2U + IR_DECODE_MAX_PULSES * 2U];
    size_t length = 0U;
    bytes[length++] = (uint8_t)count;
    bytes[length++] = (uint8_t)(count >> 8);
    for (size_t index = 0U; index < count; ++index) {
        const uint32_t mark = (pulses[index].mark_us + 100U) / 200U;
        const uint32_t space = (pulses[index].space_us + 100U) / 200U;
        bytes[length++] = (uint8_t)(mark > 255U ? 255U : mark);
        bytes[length++] = (uint8_t)(space > 255U ? 255U : space);
    }
    return fnv1a(bytes, length);
}

ir_code_t ir_decode(const ir_pulse_t *pulses, size_t count)
{
    ir_code_t code = {.kind = IR_CODE_NONE, .address = 0U, .command = 0U, .hash = 0U};
    if (pulses == NULL || count == 0U) return code;

    const ir_pulse_t leader = pulses[0];
    if (near(leader.mark_us, NEC_LEADER_MARK_US, 1200U)) {
        if (near(leader.space_us, NEC_REPEAT_SPACE_US, 500U)) {
            code.kind = IR_CODE_NEC_REPEAT;
            return code;
        }
        uint32_t bits;
        if (near(leader.space_us, NEC_LEADER_SPACE_US, 800U) &&
            nec_decode_bits(pulses, count, &bits)) {
            const uint8_t address_low = (uint8_t)bits;
            const uint8_t address_high = (uint8_t)(bits >> 8);
            const uint8_t command = (uint8_t)(bits >> 16);
            const uint8_t command_check = (uint8_t)(bits >> 24);
            /* The command's complement is what tells a frame from noise; the
             * address's is only required by the plain form - extended NEC
             * spends both bytes on the address. */
            if ((uint8_t)~command == command_check) {
                code.kind = IR_CODE_NEC;
                code.command = command;
                code.address = (uint8_t)~address_low == address_high
                                   ? address_low
                                   : (uint16_t)(address_low | ((uint16_t)address_high << 8));
                return code;
            }
        }
    }

    /* Too short to be a key at all: a stray reflection, a receiver settling. */
    if (count < 4U) return code;
    code.kind = IR_CODE_RAW;
    code.hash = raw_hash(pulses, count);
    return code;
}

void ir_code_format(const ir_code_t *code, char *out, size_t size)
{
    if (out == NULL || size == 0U) return;
    if (code == NULL) {
        out[0] = '\0';
        return;
    }
    switch (code->kind) {
    case IR_CODE_NEC:
        snprintf(out, size, "nec:%x:%02x", (unsigned)code->address, (unsigned)code->command);
        break;
    case IR_CODE_NEC_REPEAT:
        snprintf(out, size, "nec-repeat");
        break;
    case IR_CODE_RAW:
        snprintf(out, size, "raw:%08x", (unsigned)code->hash);
        break;
    default:
        out[0] = '\0';
        break;
    }
}

bool ir_code_parse(const char *text, ir_code_t *code)
{
    if (text == NULL || code == NULL) return false;
    unsigned address;
    unsigned command;
    unsigned hash;
    if (sscanf(text, "nec:%x:%x", &address, &command) == 2 && address <= 0xFFFFU &&
        command <= 0xFFU) {
        *code = (ir_code_t){.kind = IR_CODE_NEC, .address = (uint16_t)address,
                            .command = (uint8_t)command, .hash = 0U};
        return true;
    }
    if (sscanf(text, "raw:%x", &hash) == 1) {
        *code = (ir_code_t){.kind = IR_CODE_RAW, .address = 0U, .command = 0U, .hash = hash};
        return true;
    }
    return false;
}

bool ir_code_same_key(const ir_code_t *left, const ir_code_t *right)
{
    if (left == NULL || right == NULL || left->kind != right->kind) return false;
    switch (left->kind) {
    case IR_CODE_NEC: return left->address == right->address && left->command == right->command;
    case IR_CODE_RAW: return left->hash == right->hash;
    default: return false;
    }
}
