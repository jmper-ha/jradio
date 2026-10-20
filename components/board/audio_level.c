#include "audio_level.h"

#include <math.h>
#include <string.h>

#include "audio_volume.h"

/* BS.1770's blocks are 400 ms; the standard overlaps them by 75 % for its
 * momentary meter, which a running 25 s mean does not need. */
#define BLOCK_MS 400U
// Below this a block is silence (the standard's absolute gate).
#define GATE_ABSOLUTE_LUFS (-70.0f)
// This far under the estimate a block is a pause or an intro (the relative gate).
#define GATE_RELATIVE_LU 10.0f
/* The running mean's memory: a plain average until this much has been heard,
 * so the first estimate is not dragged towards zero, then an exponential one
 * that forgets at the same rate. */
#define WINDOW_BLOCKS 62U /* 25 s */
// An estimate is trusted - and the relative gate used - after this much.
#define MEASURE_BLOCKS 8U /* 3.2 s */
// A station never heard is caught up with quickly for this long.
#define FAST_BLOCKS (MEASURE_BLOCKS + 50U) /* 20 s more */
/* What is worth keeping for the next time: 20 s of sound. A first estimate
 * from 3 s may be a jingle, an advert or the presenter, and a station that
 * starts from a remembered level only refines it at the slow rate - a bad
 * one kept would be heard for a minute at every start. */
#define KEEP_BLOCKS 50U /* 20 s */
// And it is settled once this much has been heard and the correction is there.
#define SETTLE_BLOCKS 75U /* 30 s */

#define FAST_DB_PER_S 1.0f
#define SLOW_DB_PER_S 0.1f
// Switched off, the correction goes in about a second.
#define OFF_DB_PER_S 10.0f
/* The slow phase sits still while it is this close: an estimate of a 25 s
 * window wanders by a fraction of a decibel, and following it would be a
 * level forever in motion for nothing anyone hears. */
#define DEAD_BAND_DB 0.5f
#define ARRIVED_DB 0.05f

static const float k_pi = 3.14159265358979f;

void audio_level_init(audio_level_t *level)
{
    if (level == NULL) return;
    memset(level, 0, sizeof(*level));
}

/* The K-weighting's analogue prototype from BS.1770, mapped to any rate by
 * the bilinear transform - the standard prints coefficients for 48 kHz only,
 * and streams come at 22.05 to 96. These are the constants libebur128 uses. */
void audio_level_k_filter(uint32_t rate, audio_biquad_t out[2])
{
    const float fs = (float)rate;
    {
        const float f0 = 1681.974450955533f;
        const float gain_db = 3.999843853973347f;
        const float q = 0.7071752369554196f;
        const float k = tanf(k_pi * f0 / fs);
        const float vh = powf(10.0f, gain_db / 20.0f);
        const float vb = powf(vh, 0.4996667741545416f);
        const float a0 = 1.0f + k / q + k * k;
        out[0].b0 = (vh + vb * k / q + k * k) / a0;
        out[0].b1 = 2.0f * (k * k - vh) / a0;
        out[0].b2 = (vh - vb * k / q + k * k) / a0;
        out[0].a1 = 2.0f * (k * k - 1.0f) / a0;
        out[0].a2 = (1.0f - k / q + k * k) / a0;
    }
    {
        const float f0 = 38.13547087602444f;
        const float q = 0.5003270373238773f;
        const float k = tanf(k_pi * f0 / fs);
        const float a0 = 1.0f + k / q + k * k;
        out[1].b0 = 1.0f;
        out[1].b1 = -2.0f;
        out[1].b2 = 1.0f;
        out[1].a1 = 2.0f * (k * k - 1.0f) / a0;
        out[1].a2 = (1.0f - k / q + k * k) / a0;
    }
}

static float power_to_lufs(float power)
{
    return -0.691f + 10.0f * log10f(power);
}

static float clamp_db(float db)
{
    if (db > AUDIO_LEVEL_LIMIT_DB) return AUDIO_LEVEL_LIMIT_DB;
    if (db < -AUDIO_LEVEL_LIMIT_DB) return -AUDIO_LEVEL_LIMIT_DB;
    return db;
}

float audio_level_correction_db(float loudness_lufs)
{
    if (!isfinite(loudness_lufs)) return 0.0f;
    return clamp_db(AUDIO_LEVEL_TARGET_LUFS - loudness_lufs);
}

static void restart(audio_level_t *level)
{
    memset(level->state, 0, sizeof(level->state));
    level->block_sum = 0.0f;
    level->block_count = 0U;
    level->mean_power = 0.0f;
    level->blocks = 0U;
    level->moving = false;
}

void audio_level_set_enabled(audio_level_t *level, bool enabled)
{
    if (level == NULL || level->enabled == enabled) return;
    level->enabled = enabled;
    if (!enabled) {
        level->wanted_db = 0.0f;
        level->remembered = false;
        level->fixed = false;
        restart(level);
    }
}

void audio_level_set_nudge(audio_level_t *level, int nudge_db)
{
    if (level == NULL) return;
    if (nudge_db > AUDIO_LEVEL_NUDGE_LIMIT_DB) nudge_db = AUDIO_LEVEL_NUDGE_LIMIT_DB;
    if (nudge_db < -AUDIO_LEVEL_NUDGE_LIMIT_DB) nudge_db = -AUDIO_LEVEL_NUDGE_LIMIT_DB;
    level->nudge_db = (int8_t)nudge_db;
}

void audio_level_begin(audio_level_t *level, float start_db, bool remembered, bool fixed)
{
    if (level == NULL || !level->enabled) return;
    restart(level);
    level->remembered = remembered || fixed;
    level->fixed = fixed;
    level->wanted_db = clamp_db(isfinite(start_db) ? start_db : 0.0f);
    // A jump, ramped by the caller across one block: the start of a station
    // is a fade-in already, and creeping up to its level would be heard.
    level->gain_db = level->wanted_db;
}

static void finish_block(audio_level_t *level)
{
    const float power = level->block_sum / (float)level->block_count;
    level->block_sum = 0.0f;
    level->block_count = 0U;
    if (!(power > 0.0f)) return;
    const float block_lufs = power_to_lufs(power);
    if (block_lufs < GATE_ABSOLUTE_LUFS) return;
    if (level->blocks >= MEASURE_BLOCKS &&
        block_lufs < power_to_lufs(level->mean_power) - GATE_RELATIVE_LU) {
        return;
    }
    const uint32_t n = level->blocks < WINDOW_BLOCKS ? level->blocks + 1U : WINDOW_BLOCKS;
    level->mean_power += (power - level->mean_power) / (float)n;
    level->blocks++;
    if (level->blocks >= MEASURE_BLOCKS) {
        level->wanted_db = clamp_db(AUDIO_LEVEL_TARGET_LUFS - power_to_lufs(level->mean_power));
    }
}

static void move_gain(audio_level_t *level, float seconds)
{
    const float diff = level->wanted_db - level->gain_db;
    float rate;
    if (!level->enabled) {
        rate = OFF_DB_PER_S;
    } else if (!level->remembered && level->blocks < FAST_BLOCKS) {
        rate = FAST_DB_PER_S;
    } else {
        if (fabsf(diff) > DEAD_BAND_DB) level->moving = true;
        if (!level->moving) return;
        rate = SLOW_DB_PER_S;
    }
    const float step = rate * seconds;
    if (fabsf(diff) <= step) {
        level->gain_db = level->wanted_db;
        level->moving = false;
    } else {
        level->gain_db += diff > 0.0f ? step : -step;
    }
    if (fabsf(level->wanted_db - level->gain_db) < ARRIVED_DB) level->moving = false;
}

void audio_level_measure(audio_level_t *level, uint32_t rate, const int16_t *in, size_t frames)
{
    if (level == NULL || in == NULL || frames == 0U || rate == 0U) return;
    const float seconds = (float)frames / (float)rate;
    if (!level->enabled || level->fixed) {
        move_gain(level, seconds);
        return;
    }
    if (rate != level->rate) {
        level->rate = rate;
        audio_level_k_filter(rate, level->k);
        level->block_frames = rate * BLOCK_MS / 1000U;
        memset(level->state, 0, sizeof(level->state));
        level->block_sum = 0.0f;
        level->block_count = 0U;
    }
    /* Both stages for both channels in one pass, the state in registers -
     * the same shape as the tone's shelves, for the same reason. */
    const audio_biquad_t *s1 = &level->k[0];
    const audio_biquad_t *s2 = &level->k[1];
    float l11 = level->state[0][0].z1, l12 = level->state[0][0].z2;
    float l21 = level->state[1][0].z1, l22 = level->state[1][0].z2;
    float r11 = level->state[0][1].z1, r12 = level->state[0][1].z2;
    float r21 = level->state[1][1].z1, r22 = level->state[1][1].z2;
    const float scale = 1.0f / 32768.0f;
    for (size_t i = 0U; i < frames; ++i) {
        const float left = (float)in[i * 2U] * scale;
        const float right = (float)in[i * 2U + 1U] * scale;
        const float yl = s1->b0 * left + l11;
        const float yr = s1->b0 * right + r11;
        l11 = s1->b1 * left - s1->a1 * yl + l12;
        r11 = s1->b1 * right - s1->a1 * yr + r12;
        l12 = s1->b2 * left - s1->a2 * yl;
        r12 = s1->b2 * right - s1->a2 * yr;
        const float zl = s2->b0 * yl + l21;
        const float zr = s2->b0 * yr + r21;
        l21 = s2->b1 * yl - s2->a1 * zl + l22;
        r21 = s2->b1 * yr - s2->a1 * zr + r22;
        l22 = s2->b2 * yl - s2->a2 * zl;
        r22 = s2->b2 * yr - s2->a2 * zr;
        level->block_sum += zl * zl + zr * zr;
        if (++level->block_count >= level->block_frames) finish_block(level);
    }
    // A state this small is silence; kept, it would be a denormal to grind on.
    const float tiny = 1e-12f;
    level->state[0][0].z1 = fabsf(l11) < tiny ? 0.0f : l11;
    level->state[0][0].z2 = fabsf(l12) < tiny ? 0.0f : l12;
    level->state[1][0].z1 = fabsf(l21) < tiny ? 0.0f : l21;
    level->state[1][0].z2 = fabsf(l22) < tiny ? 0.0f : l22;
    level->state[0][1].z1 = fabsf(r11) < tiny ? 0.0f : r11;
    level->state[0][1].z2 = fabsf(r12) < tiny ? 0.0f : r12;
    level->state[1][1].z1 = fabsf(r21) < tiny ? 0.0f : r21;
    level->state[1][1].z2 = fabsf(r22) < tiny ? 0.0f : r22;
    move_gain(level, seconds);
}

uint16_t audio_level_apply(audio_level_t *level, uint16_t volume_gain)
{
    if (level == NULL) return volume_gain;
    level->capped = false;
    const float db = level->gain_db + (float)level->nudge_db;
    if (db == 0.0f || volume_gain == 0U) return volume_gain;
    const float gain = (float)volume_gain * powf(10.0f, db / 20.0f);
    if (gain >= (float)AUDIO_VOLUME_UNITY) {
        level->capped = db > 0.0f;
        return (uint16_t)AUDIO_VOLUME_UNITY;
    }
    return (uint16_t)(gain + 0.5f);
}

bool audio_level_idle(const audio_level_t *level)
{
    return level == NULL ||
           (!level->enabled && level->gain_db == 0.0f && level->nudge_db == 0);
}

void audio_level_status(const audio_level_t *level, audio_level_status_t *status)
{
    if (status == NULL) return;
    memset(status, 0, sizeof(*status));
    if (level == NULL) return;
    status->nudge_db = level->nudge_db;
    status->capped = level->capped;
    if (!level->enabled) return;
    status->gain_db = level->gain_db;
    status->wanted_db = level->wanted_db;
    status->loudness = level->blocks > 0U ? power_to_lufs(level->mean_power) : 0.0f;
    status->fixed = level->fixed;
    status->keep = !level->fixed && level->blocks >= KEEP_BLOCKS;
    if (level->fixed) {
        status->phase = AUDIO_LEVEL_SETTLED;
    } else if (!level->remembered && level->blocks < MEASURE_BLOCKS) {
        status->phase = AUDIO_LEVEL_MEASURING;
    } else if (!level->moving && fabsf(level->wanted_db - level->gain_db) <= DEAD_BAND_DB &&
               (level->remembered || level->blocks >= SETTLE_BLOCKS)) {
        status->phase = AUDIO_LEVEL_SETTLED;
    } else {
        status->phase = AUDIO_LEVEL_LEARNING;
    }
}
