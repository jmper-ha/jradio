#include "rds.h"

#include <stddef.h>
#include <string.h>

#define RDS_CARRIAGE_RETURN 0x0DU

void rds_decoder_reset(rds_decoder_t *decoder)
{
    if (decoder == NULL) return;
    const uint32_t revision = decoder->revision;
    const bool had_text = decoder->ps_text[0] != '\0' || decoder->rt_text[0] != '\0';
    memset(decoder, 0, sizeof(*decoder));
    decoder->rt_flag = -1;
    decoder->rt_end = RDS_RT_LEN;
    // Clearing what was shown is a change the faces have to hear about.
    decoder->revision = had_text ? revision + 1U : revision;
}

/* One character as UTF-8, or nothing for a control code. */
static size_t rds_utf8(uint8_t code, char *out)
{
    if (code >= 0x20U && code <= 0x7EU) {
        out[0] = (char)code;
        return 1U;
    }
    uint16_t point = 0U;
    if (code >= 0xC0U) {
        point = (uint16_t)(0x0410U + (code - 0xC0U));  // А..я
    } else if (code == 0xA8U) {
        point = 0x0401U;  // Ё
    } else if (code == 0xB8U) {
        point = 0x0451U;  // ё
    } else if (code >= 0x80U) {
        out[0] = '?';
        return 1U;
    } else {
        return 0U;
    }
    out[0] = (char)(0xC0U | (point >> 6));
    out[1] = (char)(0x80U | (point & 0x3FU));
    return 2U;
}

/* The characters as text, the station's padding spaces off both ends. */
static void rds_text(const uint8_t *codes, size_t count, char *out, size_t out_size)
{
    size_t first = 0U;
    while (first < count && codes[first] == ' ') ++first;
    size_t last = count;
    while (last > first && (codes[last - 1U] == ' ' || codes[last - 1U] == 0U)) --last;
    size_t used = 0U;
    for (size_t i = first; i < last; ++i) {
        char bytes[2];
        const size_t length = rds_utf8(codes[i], bytes);
        if (used + length + 1U > out_size) break;
        memcpy(out + used, bytes, length);
        used += length;
    }
    out[used] = '\0';
}

static bool rds_publish(char *text, size_t size, const uint8_t *codes, size_t count)
{
    char next[RDS_RT_TEXT_MAX];
    rds_text(codes, count, next, size < sizeof(next) ? size : sizeof(next));
    if (strcmp(text, next) == 0) return false;
    memcpy(text, next, strlen(next) + 1U);
    return true;
}

static bool rds_all_blank(const uint8_t *codes, size_t count)
{
    for (size_t i = 0U; i < count; ++i) {
        if (codes[i] != ' ' && codes[i] != 0U) return false;
    }
    return true;
}

static bool rds_feed_ps(rds_decoder_t *decoder, uint16_t block_b, uint16_t block_d)
{
    const uint8_t segment = (uint8_t)(block_b & 0x3U);
    const uint8_t bit = (uint8_t)(1U << segment);
    const uint8_t chars[2] = {(uint8_t)(block_d >> 8), (uint8_t)block_d};
    uint8_t *candidate = &decoder->ps_candidate[segment * 2U];
    if ((decoder->ps_candidates & bit) == 0U || memcmp(candidate, chars, 2U) != 0) {
        /* Something else in this place than last time: the station has
         * moved on to its next message, or the group is noise. Either way
         * nothing put together so far can be trusted to belong with what
         * comes next. Stations that rotate their name - on the bench one
         * went "*95.6FM*", "*ZVEZDA*", "*3BE3DA*" and eight spaces - came out
         * as a piece of one and a piece of another ("M*") until this cleared
         * everything rather than only a piece already confirmed. Each
         * message is sent over and over, so the whole of it comes round. */
        if ((decoder->ps_candidates & bit) != 0U) decoder->ps_segments = 0U;
        memcpy(candidate, chars, 2U);
        decoder->ps_candidates |= bit;
        return false;
    }
    // Seen twice alike: this is what the station is sending.
    memcpy(&decoder->ps[segment * 2U], chars, 2U);
    decoder->ps_segments |= bit;
    if (decoder->ps_segments != 0x0FU) return false;
    /* The blank message some stations put between the others is not a
     * name: the one before stays up. */
    if (rds_all_blank(decoder->ps, RDS_PS_LEN)) return false;
    return rds_publish(decoder->ps_text, sizeof(decoder->ps_text), decoder->ps, RDS_PS_LEN);
}

static bool rds_feed_rt(rds_decoder_t *decoder, uint16_t block_b, uint16_t block_c,
                        uint16_t block_d, bool version_b)
{
    const int8_t flag = (int8_t)((block_b >> 4) & 0x1U);
    const uint8_t per_segment = version_b ? 2U : 4U;
    /* A new text, by the station's own flag, or a switch between the two
     * versions: what was collected belongs to the old one. */
    if (flag != decoder->rt_flag || per_segment != decoder->rt_segment_chars) {
        memset(decoder->rt, ' ', sizeof(decoder->rt));
        decoder->rt_segments = 0U;
        decoder->rt_end = (uint8_t)(per_segment * 16U);
        decoder->rt_flag = flag;
        decoder->rt_segment_chars = per_segment;
    }
    const uint8_t segment = (uint8_t)(block_b & 0xFU);
    uint8_t chars[4];
    if (version_b) {
        chars[0] = (uint8_t)(block_d >> 8);
        chars[1] = (uint8_t)block_d;
    } else {
        chars[0] = (uint8_t)(block_c >> 8);
        chars[1] = (uint8_t)block_c;
        chars[2] = (uint8_t)(block_d >> 8);
        chars[3] = (uint8_t)block_d;
    }
    const uint8_t at = (uint8_t)(segment * per_segment);
    for (uint8_t i = 0U; i < per_segment; ++i) {
        if (chars[i] == RDS_CARRIAGE_RETURN && at + i < decoder->rt_end) {
            decoder->rt_end = (uint8_t)(at + i);
        }
        decoder->rt[at + i] = chars[i];
    }
    decoder->rt_segments |= 1UL << segment;
    // Every segment up to the end has to be in before the text is shown.
    const uint8_t needed = (uint8_t)((decoder->rt_end + per_segment - 1U) / per_segment);
    const uint32_t mask = needed >= 32U ? 0xFFFFFFFFUL : ((1UL << needed) - 1UL);
    if ((decoder->rt_segments & mask) != mask) return false;
    return rds_publish(decoder->rt_text, sizeof(decoder->rt_text), decoder->rt, decoder->rt_end);
}

bool rds_decoder_heard(const rds_decoder_t *decoder)
{
    return decoder != NULL && decoder->pi_repeats >= RDS_HEARD_REPEATS;
}

bool rds_decoder_feed(rds_decoder_t *decoder, const uint16_t blocks[4], bool block_a_ok,
                      bool block_b_ok)
{
    if (decoder == NULL || blocks == NULL || !block_b_ok) return false;
    /* Another station's code: nothing collected so far is about it. Version
     * B groups repeat the code in block C, but block A always has it. */
    if (block_a_ok && blocks[0] != 0U && blocks[0] != decoder->pi) {
        /* And not on one sighting: the chip calls block A clean on groups
         * whose code is plainly noise - 0xEE60 and 0xF730 among 0x7730 on
         * the bench - and each of those used to throw the name away. A
         * code is another station's once it has come twice in a row. */
        if (decoder->pi == 0U || blocks[0] == decoder->pi_candidate) {
            if (decoder->pi != 0U) rds_decoder_reset(decoder);
            decoder->pi = blocks[0];
            decoder->pi_repeats = 1U;
        } else {
            decoder->pi_candidate = blocks[0];
            decoder->pi_repeats = 0U;
            return false;
        }
    } else if (block_a_ok && blocks[0] == decoder->pi) {
        decoder->pi_candidate = 0U;
        if (decoder->pi_repeats < UINT8_MAX) ++decoder->pi_repeats;
    }
    const uint8_t type = (uint8_t)(blocks[1] >> 12);
    const bool version_b = (blocks[1] & 0x0800U) != 0U;
    bool changed = false;
    if (type == 0U) {
        changed = rds_feed_ps(decoder, blocks[1], blocks[3]);
    } else if (type == 2U) {
        changed = rds_feed_rt(decoder, blocks[1], blocks[2], blocks[3], version_b);
    }
    if (changed) ++decoder->revision;
    return changed;
}
