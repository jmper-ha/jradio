#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* RDS, the data an FM station sends beside its sound: the station's name
 * (PS, eight characters, groups 0A/0B) and its radiotext (RT, up to 64
 * characters, groups 2A/2B) - what is playing, or whatever the station feels
 * like saying. The tuner hands over one group of four 16-bit blocks at a
 * time; this collects them into text. Pure, so it is tested on the host.
 *
 * The air is noisy and the chip only reports errors for blocks A and B, so a
 * piece of the name is taken only when it has arrived twice the same: one
 * bad block C or D would otherwise put a wrong letter in the name for as long
 * as the station is on. For the same reason a changed piece throws away the
 * rest of the name, and a new station code has to arrive twice in a row
 * before it counts as another station.
 *
 * Text comes out as UTF-8. The standard's own table puts accented Latin in
 * the upper half; Russian stations send windows-1251 there instead, and that
 * is what this reads it as - the one that is actually on the air here. */

#define RDS_PS_LEN 8
#define RDS_RT_LEN 64
// Every character can take two bytes in UTF-8, plus the terminator.
#define RDS_PS_TEXT_MAX (RDS_PS_LEN * 2 + 1)
#define RDS_RT_TEXT_MAX (RDS_RT_LEN * 2 + 1)

typedef struct {
    uint16_t pi;
    uint16_t pi_candidate;  // a different code seen once, waiting for a second
    uint8_t pi_repeats;     // clean sightings of `pi` in a row
    uint8_t ps[RDS_PS_LEN];
    uint8_t ps_candidate[RDS_PS_LEN];
    uint8_t ps_segments;        // bit per two-character segment, confirmed
    uint8_t ps_candidates;      // bit per segment with a first sighting waiting
    uint8_t rt[RDS_RT_LEN];
    uint32_t rt_segments;       // bit per segment received
    uint8_t rt_segment_chars;   // 4 for 2A, 2 for 2B; 0 before the first
    int8_t rt_flag;             // the A/B flag, -1 before the first
    uint8_t rt_end;             // characters before the carriage return, or RDS_RT_LEN
    char ps_text[RDS_PS_TEXT_MAX];
    char rt_text[RDS_RT_TEXT_MAX];
    // Moves whenever ps_text or rt_text does.
    uint32_t revision;
} rds_decoder_t;

void rds_decoder_reset(rds_decoder_t *decoder);

/* One group. `block_b_ok` is the chip's word that block B - which says what
 * the group is - arrived clean; a group without it is dropped. `block_a_ok`
 * says the same of the station's code, which is ignored when it did not.
 * Returns true when the published text changed. */
bool rds_decoder_feed(rds_decoder_t *decoder, const uint16_t blocks[4], bool block_a_ok,
                      bool block_b_ok);

/* Whether the station sends RDS at all: its code has come clean and the same
 * RDS_HEARD_REPEATS times running. Noise brings a code too, but a different
 * one each time. Long before a name is whole - a station that rotates four
 * messages takes seconds to show one, and the mark should not wait for it. */
#define RDS_HEARD_REPEATS 3U
bool rds_decoder_heard(const rds_decoder_t *decoder);

#ifdef __cplusplus
}
#endif
