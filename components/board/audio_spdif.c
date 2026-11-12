#include "audio_spdif.h"

#include <string.h>

/* In IRAM on the device: the encoder runs inside the DMA's interrupt, and
 * from flash every cache miss is paid there. */
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define SPDIF_FAST IRAM_ATTR
#else
#define SPDIF_FAST
#endif

/* The three preambles, as the eight cells that follow a line resting low.
 * They break the biphase rule on purpose - three cells at one level - which
 * is how a receiver finds the start of a subframe. Here the line is always
 * low before one - see spdif_subframe(). B starts a channel-status block
 * (left), M every other left subframe, W every right one. */
#define SPDIF_PREAMBLE_B 0xE8U
#define SPDIF_PREAMBLE_M 0xE2U
#define SPDIF_PREAMBLE_W 0xE4U

uint8_t audio_spdif_rate_code(uint32_t sample_rate)
{
    switch (sample_rate) {
    case 22050U: return 0x4U;
    case 24000U: return 0x6U;
    case 32000U: return 0x3U;
    case 44100U: return 0x0U;
    case 48000U: return 0x2U;
    case 88200U: return 0x8U;
    case 96000U: return 0xAU;
    case 176400U: return 0xCU;
    case 192000U: return 0xEU;
    default: return 0xFFU;
    }
}

/* The cells of a byte's eight slots, earliest in the top bit, from a line
 * resting low. The last cell is the level the line is left at, which is the
 * byte's parity: the slot transitions are eight, an even number, and each one
 * adds one more. This runs in the DMA's interrupt, two subframes for every
 * frame played - 11.8% of a core at 96 kHz done cell by cell with the level
 * carried, so it is a few lookups now. */
static uint16_t s_byte_cells[256];
/* Slots 28-31 - validity, user, channel status and parity - by the level
 * they start from and the status bit, (level << 1) | status. Parity is then
 * fixed: the level is the parity of slots 4-27 so far. */
static uint8_t s_tail_cells[4];

static void spdif_build_cells(void)
{
    for (unsigned byte = 0U; byte < 256U; ++byte) {
        uint16_t cells = 0U;
        bool level = false;
        for (unsigned bit = 0U; bit < 8U; ++bit) {
            level = !level;  // every slot starts with a transition
            cells = (uint16_t)((cells << 1U) | (level ? 1U : 0U));
            if ((byte >> bit) & 1U) level = !level;  // and a one has another
            cells = (uint16_t)((cells << 1U) | (level ? 1U : 0U));
        }
        s_byte_cells[byte] = cells;
    }
    for (unsigned index = 0U; index < 4U; ++index) {
        const unsigned level = index >> 1U;
        const unsigned status = index & 1U;
        const unsigned nibble = (status << 2U) | ((level ^ status) << 3U);
        // Slots 28-31 are the first four of a byte, so the top half of its cells.
        uint8_t cells = (uint8_t)(s_byte_cells[nibble] >> 8U);
        if (level) cells = (uint8_t)~cells;
        s_tail_cells[index] = cells;
    }
}

void audio_spdif_init(audio_spdif_encoder_t *encoder, uint32_t sample_rate)
{
    if (s_byte_cells[1] == 0U) spdif_build_cells();
    memset(encoder, 0, sizeof(*encoder));
    /* Byte 0: consumer, PCM audio (bit 1 clear - set, a receiver mutes it as
     * data), copying permitted (bit 2), no emphasis. Byte 1, the category, is
     * left general. */
    encoder->status[0] = 0x04U;
    /* A rate with no code of its own says "not indicated", bit 24 alone:
     * 44.1 kHz already owns all four clear. */
    const uint8_t code = audio_spdif_rate_code(sample_rate);
    encoder->status[3] = code == 0xFFU ? 0x1U : code;
}

/* One subframe into two words: the preamble, then time slots 4-31 in
 * biphase mark. Slots 4-11 are the low bits a 24-bit word would have, zero
 * for 16 bits; 12-27 the sample, least significant bit first; then validity
 * (0, valid), user (0), channel status, and the parity that makes slots 4-31
 * even. Even parity is also what brings the line back to the level the
 * subframe began on, and every preamble ends where it began - so every
 * subframe starts from a low line and no level is carried between them. */
static inline void SPDIF_FAST spdif_subframe(uint32_t *words, uint32_t preamble, uint16_t value,
                                             unsigned status)
{
    const uint32_t low = s_byte_cells[value & 0xFFU];
    // The high byte starts where the low one left the line.
    const uint32_t high = (s_byte_cells[value >> 8U] ^ (0U - (low & 1U))) & 0xFFFFU;
    const uint32_t tail = s_tail_cells[((high & 1U) << 1U) | status];
    // The eight zero slots from a low line, which they leave low again.
    words[0] = (preamble << 24U) | (0xCCCCU << 8U) | (low >> 8U);
    words[1] = ((low & 0xFFU) << 24U) | (high << 8U) | tail;
}

void SPDIF_FAST audio_spdif_encode_s16(audio_spdif_encoder_t *encoder, const int16_t *samples,
                                       size_t frames, uint32_t *words)
{
    uint8_t frame = encoder->frame;
    for (size_t index = 0; index < frames; ++index) {
        const unsigned status = (encoder->status[frame / 8U] >> (frame % 8U)) & 1U;
        const uint16_t left = samples != NULL ? (uint16_t)samples[2U * index] : 0U;
        const uint16_t right = samples != NULL ? (uint16_t)samples[2U * index + 1U] : 0U;
        spdif_subframe(&words[4U * index], frame == 0U ? SPDIF_PREAMBLE_B : SPDIF_PREAMBLE_M,
                       left, status);
        spdif_subframe(&words[4U * index + 2U], SPDIF_PREAMBLE_W, right, status);
        frame = (uint8_t)(frame + 1U == AUDIO_SPDIF_BLOCK_FRAMES ? 0U : frame + 1U);
    }
    encoder->frame = frame;
}
