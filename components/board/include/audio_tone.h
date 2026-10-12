#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Tone controls and loudness compensation, on the samples on their way to the
 * DAC - the same place the volume is applied, for every source that goes
 * through the S3.
 *
 * Two different corrections that add: the tone controls are for the speakers
 * and the room and stay put whatever the volume; loudness is for the ear,
 * which hears less bass - and a little less treble - the quieter the sound
 * (the equal-loudness contours), and so it lifts them more the further the
 * volume is turned down and not at all at full volume. Each has its own shelf
 * filters, so neither has to be shaped to suit the other.
 *
 * Headroom is worked out here and not left to a limiter:
 * - loudness lifts a band by no more than the volume has already taken away,
 *   so it never takes a sample past full scale on its own;
 * - a tone boost is paid for by turning everything down first - but only by
 *   the part of it the volume has not already made room for, so below the
 *   top of the scale a boost is simply a boost, and near the top the volume
 *   stops rising by that much rather than clipping;
 * - and the two together are capped per band.
 *
 * The arithmetic is in float - the S3 has an FPU - with the one rounding to
 * 16 bits at the very end. Pure, and tested on the host. */

#define AUDIO_TONE_DB_MIN (-12)
#define AUDIO_TONE_DB_MAX 12
// Tone and loudness together on one band.
#define AUDIO_TONE_SUM_MAX_DB 18.0f

typedef enum {
    AUDIO_LOUDNESS_OFF = 0,
    AUDIO_LOUDNESS_WEAK,
    AUDIO_LOUDNESS_MEDIUM,
    AUDIO_LOUDNESS_STRONG,
} audio_loudness_t;

typedef struct {
    int8_t bass_db;    /* AUDIO_TONE_DB_MIN..AUDIO_TONE_DB_MAX */
    int8_t treble_db;
    uint8_t loudness;  /* audio_loudness_t */
    bool loudness_treble;
} audio_tone_settings_t;

typedef struct {
    float b0, b1, b2, a1, a2;
} audio_biquad_t;

// Transposed direct form II.
typedef struct {
    float z1, z2;
} audio_biquad_state_t;

/* The four shelves, in the order they are applied. */
enum {
    AUDIO_TONE_BASS = 0,
    AUDIO_TONE_TREBLE,
    AUDIO_TONE_LOUD_BASS,
    AUDIO_TONE_LOUD_TREBLE,
    AUDIO_TONE_BANDS,
};

typedef struct {
    audio_tone_settings_t settings;
    uint32_t rate;
    /* What each shelf is set to now, in dB; moved towards the target a step
     * per block so a turned knob is a slope, not a step. */
    float band_db[AUDIO_TONE_BANDS];
    /* The turn-down that pays for the tone boosts, sloped the same way: a
     * boost switched off must not leave the radio louder in one step. */
    float pre_db;
    audio_biquad_t filter[AUDIO_TONE_BANDS];
    audio_biquad_state_t state[AUDIO_TONE_BANDS][2];
    bool primed;
} audio_tone_t;

void audio_tone_init(audio_tone_t *tone);

/* Whether the settings do anything at all. When not, the caller keeps its
 * bit-exact path at full volume. */
bool audio_tone_active(const audio_tone_settings_t *settings);

/* Clamps a setting into range; false when it was out of it. */
bool audio_tone_settings_valid(const audio_tone_settings_t *settings);

void audio_tone_set(audio_tone_t *tone, const audio_tone_settings_t *settings);

/* Nothing set and nothing still sloping back to nothing: the caller may go
 * back to its bit-exact path without a step in the sound. */
bool audio_tone_idle(const audio_tone_t *tone);

/* Clears the filters' memory: at the start of a stream and when the sample
 * rate changes, so the first block does not ring with the last one's tail. */
void audio_tone_reset(audio_tone_t *tone);

/* The loudness lift of one band, in dB, for a volume given as its multiplier
 * out of audio_volume_gain() (AUDIO_VOLUME_UNITY is full volume). */
float audio_tone_loudness_db(uint8_t loudness, bool treble, uint16_t volume_gain);

/* What everything is first turned down by, in dB (>= 0), at this volume:
 * the largest lift - tone and loudness at the same end together - less what
 * the volume has already taken off. Nothing at all below full volume for a
 * lift the volume has room for, so a boost sounds like a boost. */
float audio_tone_headroom_db(const audio_tone_settings_t *settings, uint16_t volume_gain);

/* `frames` interleaved stereo 16-bit frames from `in` to `out` (they may be
 * the same buffer), with the volume moving from `gain_start` to `gain_end`
 * (AUDIO_VOLUME_UNITY units) across the block, at `rate` Hz. */
void audio_tone_process(audio_tone_t *tone, uint32_t rate, const int16_t *in, int16_t *out,
                        size_t frames, uint16_t gain_start, uint16_t gain_end);

/* For the tests: a shelf's design, and the magnitude of a biquad in dB. */
audio_biquad_t audio_tone_shelf(bool high, float freq, float slope, float gain_db,
                                uint32_t rate);
float audio_tone_response_db(const audio_biquad_t *filter, float freq, uint32_t rate);
