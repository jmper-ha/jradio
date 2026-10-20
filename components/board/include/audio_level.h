#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "audio_tone.h"

/* Levelling: one station as loud as the next, without a number typed into
 * the playlist for each.
 *
 * The loudness is measured the way EBU R128 (ITU-R BS.1770) does it - a
 * K-weighting filter, then the mean power over 400 ms blocks - on what the
 * source sends, before the volume and the tone. Blocks of silence and blocks
 * far below the running estimate are left out (the standard's two gates), so
 * a pause, a quiet intro or the gap between two songs does not pull it down.
 * The estimate is a running mean of about the last 25 s: one loud song, or
 * the adverts, move it little.
 *
 * The correction is the target less the estimate, within +-9 dB, and it
 * moves slowly - quickly only for the first 20 s of a station nobody has
 * measured before, 0.1 dB/s after that - so it is heard as a station that
 * sounds right, not as a level that breathes.
 *
 * What it found is meant to be kept per station by the caller and handed back
 * the next time: a station then starts at its own level and only refines it.
 *
 * A lift is only taken out of the room the volume has left: the volume and
 * the correction together never exceed full scale, so nothing clips and no
 * limiter is needed. At full volume a quiet station therefore stays quiet,
 * and the status says so - otherwise the feature looks broken. Cuts are
 * always available. Pure, and tested on the host. */

/* What the correction aims every station at, in LUFS. Most streams are
 * mastered louder than this, so most are turned down - which always works -
 * and only the quiet ones need the volume's room. */
#define AUDIO_LEVEL_TARGET_LUFS (-16.0f)
#define AUDIO_LEVEL_LIMIT_DB 9.0f
/* The listener's own word on a station, added to whatever the levelling
 * makes of it - or the whole of it with the levelling off. Whole decibels:
 * it is set a step at a time by ear. */
#define AUDIO_LEVEL_NUDGE_LIMIT_DB 6

typedef enum {
    AUDIO_LEVEL_OFF = 0,
    // A station not measured before, with no estimate yet.
    AUDIO_LEVEL_MEASURING,
    // An estimate, and the correction still on its way to it.
    AUDIO_LEVEL_LEARNING,
    // Where it should be: remembered, measured and stable, or from tags.
    AUDIO_LEVEL_SETTLED,
} audio_level_phase_t;

typedef struct {
    uint8_t phase;      /* audio_level_phase_t */
    float gain_db;      /* the correction applied now, before the volume's cap */
    float wanted_db;    /* where it is heading */
    float loudness;     /* the estimate, LUFS; meaningless while measuring */
    bool capped;        /* a lift the volume had no room for, in part or whole */
    bool fixed;         /* given by the source (tags), not measured */
    /* Measured from enough sound - 20 s, silence not counted - to be kept
     * for the next time. Never for a correction given by the source. */
    bool keep;
    /* The listener's nudge, in whole decibels. Reported with the levelling
     * off too - the nudge works without it - while everything above is then
     * zero and the phase AUDIO_LEVEL_OFF. */
    int8_t nudge_db;
} audio_level_status_t;

typedef struct {
    bool enabled;
    uint32_t rate;
    // The K-weighting: a high shelf (the head) and a high pass.
    audio_biquad_t k[2];
    audio_biquad_state_t state[2][2];
    // The 400 ms block being summed.
    float block_sum;
    uint32_t block_count;
    uint32_t block_frames;
    // The running estimate, as mean power, and how many blocks went into it.
    float mean_power;
    uint32_t blocks;
    float gain_db;
    float wanted_db;
    bool remembered;
    bool fixed;
    // In the slow phase the correction starts moving only past a dead band.
    bool moving;
    bool capped;
    int8_t nudge_db;
} audio_level_t;

void audio_level_init(audio_level_t *level);

/* Off: the correction slopes back to nothing within about a second and no
 * more is measured. The caller begins again after switching it back on. */
void audio_level_set_enabled(audio_level_t *level, bool enabled);

/* A new station or source. `start_db` is where to start: what was learned
 * for it before (`remembered`), what its tags say (`fixed` - nothing is then
 * measured), or 0 for one never heard. The filters and the estimate start
 * afresh; the correction moves to `start_db` within the next block, which the
 * caller ramps. */
void audio_level_begin(audio_level_t *level, float start_db, bool remembered, bool fixed);

/* The listener's nudge, within +-AUDIO_LEVEL_NUDGE_LIMIT_DB. Kept across a
 * begin and across switching the levelling off: it belongs to the station,
 * and the caller says it again - 0 included - when the station changes. It
 * jumps, and the caller ramps it across the next block like the volume. */
void audio_level_set_nudge(audio_level_t *level, int nudge_db);

/* Measures `frames` interleaved stereo frames at `rate` Hz and moves the
 * correction by the time they take. Silence is fine - it is gated out. */
void audio_level_measure(audio_level_t *level, uint32_t rate, const int16_t *in, size_t frames);

/* The volume's multiplier (AUDIO_VOLUME_UNITY units) with the correction and
 * the nudge in it, never above AUDIO_VOLUME_UNITY. Records whether a lift was cut short. */
uint16_t audio_level_apply(audio_level_t *level, uint16_t volume_gain);

/* Nothing applied and nothing to slope back from: the caller may keep its
 * bit-exact path. */
bool audio_level_idle(const audio_level_t *level);

void audio_level_status(const audio_level_t *level, audio_level_status_t *status);

/* The correction for a recording whose loudness is already known - a Yandex
 * track's r128, a file's ReplayGain - so it need not be measured: the target
 * less that loudness, within the limit. */
float audio_level_correction_db(float loudness_lufs);

/* ReplayGain 2 aims at -18 LUFS and stores the gain that gets there, so the
 * loudness it measured is -18 less that gain; R128_TRACK_GAIN aims at -23 and
 * stores its gain in 1/256 dB. */
#define AUDIO_LEVEL_REPLAYGAIN_REFERENCE_LUFS (-18.0f)
#define AUDIO_LEVEL_R128_REFERENCE_LUFS (-23.0f)

/* For the tests: the K-weighting's two stages at `rate`. */
void audio_level_k_filter(uint32_t rate, audio_biquad_t out[2]);
