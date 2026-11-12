#include "audio_spdif.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* A receiver, cell by cell: what a decoder on the far end of the cable would
 * make of the words, so the test checks the stream rather than the code. */
typedef struct {
    bool cells[64];
} subframe_cells_t;

static void cells_of(const uint32_t *words, subframe_cells_t *out)
{
    for (unsigned index = 0; index < 64U; ++index) {
        const uint32_t word = words[index / 32U];
        out->cells[index] = ((word >> (31U - index % 32U)) & 1U) != 0U;
    }
}

static uint8_t preamble_of(const subframe_cells_t *sub, bool before)
{
    uint8_t pattern = 0U;
    for (unsigned index = 0; index < 8U; ++index) {
        pattern = (uint8_t)((pattern << 1U) | (sub->cells[index] ? 1U : 0U));
    }
    return before ? (uint8_t)~pattern : pattern;
}

// Slots 4-31 back to bits, checking the biphase rule as it goes.
static uint32_t slots_of(const subframe_cells_t *sub)
{
    uint32_t slots = 0U;
    for (unsigned slot = 4U; slot < 32U; ++slot) {
        const bool first = sub->cells[2U * slot];
        const bool second = sub->cells[2U * slot + 1U];
        assert(first != sub->cells[2U * slot - 1U]);  // a transition at every slot
        if (first != second) slots |= 1UL << slot;
    }
    return slots;
}

static unsigned parity(uint32_t value)
{
    unsigned ones = 0U;
    for (; value != 0U; value >>= 1U) ones += value & 1U;
    return ones & 1U;
}

static void test_samples_come_back_with_their_preambles_and_parity(void)
{
    audio_spdif_encoder_t encoder;
    audio_spdif_init(&encoder, 48000U);
    enum { FRAMES = 2U * AUDIO_SPDIF_BLOCK_FRAMES + 5U };
    static int16_t samples[2U * FRAMES];
    static uint32_t words[AUDIO_SPDIF_WORDS_PER_FRAME * FRAMES];
    for (unsigned index = 0; index < 2U * FRAMES; ++index) {
        samples[index] = (int16_t)(index * 7919U - 32768);
    }
    samples[0] = INT16_MIN;
    samples[1] = INT16_MAX;
    // In three calls, so a block and the line level carry across them.
    audio_spdif_encode_s16(&encoder, samples, 100U, words);
    audio_spdif_encode_s16(&encoder, &samples[200], 200U, &words[400]);
    audio_spdif_encode_s16(&encoder, &samples[600], FRAMES - 300U, &words[1200]);

    bool level = false;  // the line rests low before the first cell
    uint8_t status[AUDIO_SPDIF_BLOCK_FRAMES / 8U] = {0};
    for (unsigned frame = 0; frame < FRAMES; ++frame) {
        for (unsigned channel = 0; channel < 2U; ++channel) {
            subframe_cells_t sub;
            cells_of(&words[4U * frame + 2U * channel], &sub);
            const uint8_t expected = channel == 1U ? 0xE4U
                                     : frame % AUDIO_SPDIF_BLOCK_FRAMES == 0U ? 0xE8U
                                                                              : 0xE2U;
            assert(preamble_of(&sub, level) == expected);
            const uint32_t slots = slots_of(&sub);
            assert((slots & 0xFF0U) == 0U);                   // no low bits for 16-bit
            assert(((slots >> 12U) & 0xFFFFU) == (uint16_t)samples[2U * frame + channel]);
            assert((slots & (1UL << 28U)) == 0U);             // valid
            assert(parity(slots >> 4U) == 0U);                // even over slots 4-31
            if (frame < AUDIO_SPDIF_BLOCK_FRAMES && channel == 0U && (slots & (1UL << 30U))) {
                status[frame / 8U] |= (uint8_t)(1U << (frame % 8U));
            }
            level = sub.cells[63];
        }
    }
    // PCM, copying permitted, 48 kHz in bits 24-27.
    assert(status[0] == 0x04U);
    assert(status[3] == 0x02U);
    for (unsigned index = 4U; index < sizeof(status); ++index) assert(status[index] == 0U);
}

static void test_the_rates_have_their_codes(void)
{
    assert(audio_spdif_rate_code(44100U) == 0x0U);
    assert(audio_spdif_rate_code(48000U) == 0x2U);
    assert(audio_spdif_rate_code(32000U) == 0x3U);
    assert(audio_spdif_rate_code(96000U) == 0xAU);
    assert(audio_spdif_rate_code(192000U) == 0xEU);
    assert(audio_spdif_rate_code(11025U) == 0xFFU);
    audio_spdif_encoder_t encoder;
    audio_spdif_init(&encoder, 11025U);
    assert(encoder.status[3] == 0x1U);  // not indicated
}

static void test_no_samples_is_silence_on_the_same_line(void)
{
    audio_spdif_encoder_t with_zeros;
    audio_spdif_encoder_t with_null;
    audio_spdif_init(&with_zeros, 44100U);
    audio_spdif_init(&with_null, 44100U);
    static const int16_t start[2U * 7U] = {1, -1, 300, -300, 32767, -32768, 5, 9, 1, 2, 3, 4, 5, 6};
    static const int16_t zeros[2U * 250U];
    static uint32_t expected[AUDIO_SPDIF_WORDS_PER_FRAME * 250U];
    static uint32_t got[AUDIO_SPDIF_WORDS_PER_FRAME * 250U];
    // Mid-block and with the line left high or low by real samples first.
    audio_spdif_encode_s16(&with_zeros, start, 7U, expected);
    audio_spdif_encode_s16(&with_null, start, 7U, got);
    audio_spdif_encode_s16(&with_zeros, zeros, 250U, expected);
    audio_spdif_encode_s16(&with_null, NULL, 250U, got);
    assert(memcmp(expected, got, sizeof(got)) == 0);
    assert(with_zeros.frame == with_null.frame);
}

int main(void)
{
    test_no_samples_is_silence_on_the_same_line();
    test_samples_come_back_with_their_preambles_and_parity();
    test_the_rates_have_their_codes();
    return 0;
}
