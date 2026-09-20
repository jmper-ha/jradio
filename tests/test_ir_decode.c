#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ir_decode.h"

/* A frame as a receiver hands it over: the leader, then 32 bits least
   significant first, each a mark and a space. `jitter` stretches every
   mark and shrinks every space, the way a real receiver does. */
static size_t nec_frame(ir_pulse_t *out, uint8_t address, uint8_t command, int jitter)
{
    const uint32_t bits = (uint32_t)address | ((uint32_t)(uint8_t)~address << 8) |
                          ((uint32_t)command << 16) | ((uint32_t)(uint8_t)~command << 24);
    size_t count = 0U;
    out[count++] = (ir_pulse_t){.mark_us = (uint16_t)(9000 + jitter), .space_us = (uint16_t)(4500 - jitter)};
    for (unsigned bit = 0U; bit < 32U; ++bit) {
        const bool one = (bits >> bit) & 1U;
        out[count++] = (ir_pulse_t){.mark_us = (uint16_t)(562 + jitter),
                                    .space_us = (uint16_t)((one ? 1687 : 562) - jitter)};
    }
    return count;
}

static void test_a_datasheet_frame_decodes(void)
{
    ir_pulse_t pulses[40];
    const size_t count = nec_frame(pulses, 0x40, 0x12, 0);
    const ir_code_t code = ir_decode(pulses, count);
    assert(code.kind == IR_CODE_NEC);
    assert(code.address == 0x40);
    assert(code.command == 0x12);
    char text[24];
    ir_code_format(&code, text, sizeof(text));
    assert(strcmp(text, "nec:40:12") == 0);
}

static void test_a_receivers_jitter_is_tolerated(void)
{
    ir_pulse_t pulses[40];
    for (int jitter = -150; jitter <= 250; jitter += 50) {
        const size_t count = nec_frame(pulses, 0x40, 0x12, jitter);
        const ir_code_t code = ir_decode(pulses, count);
        assert(code.kind == IR_CODE_NEC);
        assert(code.address == 0x40 && code.command == 0x12);
    }
    /* The last bit's space is often the frame gap rather than a space. */
    size_t count = nec_frame(pulses, 0x40, 0x12, 0);
    pulses[count - 1U].space_us = 40000U;
    assert(ir_decode(pulses, count).command == 0x12);
}

static void test_the_repeat_frame_and_a_broken_one(void)
{
    ir_pulse_t repeat[2] = {{9000, 2250}, {562, 40000}};
    assert(ir_decode(repeat, 2).kind == IR_CODE_NEC_REPEAT);

    /* A complement that does not check is not a key - it is hashed as raw,
       so a remote with a NEC-shaped but non-NEC frame still learns. */
    ir_pulse_t pulses[40];
    const size_t count = nec_frame(pulses, 0x40, 0x12, 0);
    pulses[20].space_us = 1687;  /* flips a bit of the command's complement */
    const ir_code_t code = ir_decode(pulses, count);
    assert(code.kind == IR_CODE_RAW);

    /* Cut short: raw too, and a different signature from the whole frame. */
    const ir_code_t whole = ir_decode(pulses, count);
    const ir_code_t cut = ir_decode(pulses, count - 5U);
    assert(cut.kind == IR_CODE_RAW && cut.hash != whole.hash);
}

static void test_extended_nec_keeps_both_address_bytes(void)
{
    ir_pulse_t pulses[40];
    /* Address 0x40BF in the extended sense would be plain NEC (0xBF is
       ~0x40); 0x04 0x08 is not, so both bytes are the address. */
    const uint32_t bits = 0x04U | (0x08U << 8) | (0x12U << 16) | ((uint32_t)(uint8_t)~0x12U << 24);
    size_t count = 0U;
    pulses[count++] = (ir_pulse_t){9000, 4500};
    for (unsigned bit = 0U; bit < 32U; ++bit) {
        pulses[count++] = (ir_pulse_t){562, (uint16_t)(((bits >> bit) & 1U) ? 1687 : 562)};
    }
    const ir_code_t code = ir_decode(pulses, count);
    assert(code.kind == IR_CODE_NEC);
    assert(code.address == 0x0804);
    assert(code.command == 0x12);
}

static void test_raw_hash_is_stable_under_jitter_and_told_apart_by_key(void)
{
    ir_pulse_t a[12], b[12], c[12];
    for (size_t index = 0U; index < 12U; ++index) {
        a[index] = (ir_pulse_t){(uint16_t)(400 + 300 * (index % 3)), (uint16_t)(600 + 200 * (index % 2))};
        b[index] = (ir_pulse_t){(uint16_t)(a[index].mark_us + 40), (uint16_t)(a[index].space_us - 40)};
        c[index] = a[index];
    }
    c[5].space_us += 800;
    const ir_code_t ka = ir_decode(a, 12), kb = ir_decode(b, 12), kc = ir_decode(c, 12);
    assert(ka.kind == IR_CODE_RAW && kb.kind == IR_CODE_RAW);
    assert(ka.hash == kb.hash);
    assert(ka.hash != kc.hash);
    assert(ir_code_same_key(&ka, &kb));
    assert(!ir_code_same_key(&ka, &kc));

    /* Too short to be anything. */
    assert(ir_decode(a, 2).kind == IR_CODE_NONE);
    assert(ir_decode(NULL, 5).kind == IR_CODE_NONE);
}

static void test_the_text_form_round_trips(void)
{
    ir_code_t code;
    assert(ir_code_parse("nec:40:12", &code) && code.kind == IR_CODE_NEC && code.address == 0x40 && code.command == 0x12);
    assert(ir_code_parse("nec:804:ff", &code) && code.address == 0x804 && code.command == 0xff);
    assert(ir_code_parse("raw:9f3e21c7", &code) && code.kind == IR_CODE_RAW && code.hash == 0x9f3e21c7U);
    assert(!ir_code_parse("nec:40", &code));
    assert(!ir_code_parse("nec:40:1ff", &code));
    assert(!ir_code_parse("", &code));
    assert(!ir_code_parse("nec-repeat", &code));
    char text[24];
    ir_code_format(&code, text, sizeof(text));
    assert(strcmp(text, "raw:9f3e21c7") == 0);
    const ir_code_t none = {.kind = IR_CODE_NONE};
    ir_code_format(&none, text, sizeof(text));
    assert(text[0] == '\0');
}

int main(void)
{
    test_a_datasheet_frame_decodes();
    test_a_receivers_jitter_is_tolerated();
    test_the_repeat_frame_and_a_broken_one();
    test_extended_nec_keeps_both_address_bytes();
    test_raw_hash_is_stable_under_jitter_and_told_apart_by_key();
    test_the_text_form_round_trips();
    printf("ir_decode tests passed\n");
    return 0;
}
