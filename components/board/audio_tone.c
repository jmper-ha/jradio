#include "audio_tone.h"

#include <math.h>
#include <string.h>

#include "audio_volume.h"

/* The shelves: where each sits and how gently it rises.
 *
 * The tone controls are the gentle shelves of an amplifier's knobs, hinged
 * well inside what a small speaker plays: bass half-way up at 250 Hz and
 * nearly whole by 100, treble half-way at 4 kHz and nearly whole by 8. They
 * were first put at 100 Hz and 10 kHz, the textbook corners, and on the
 * bench speaker +12 was barely audible - what they lifted was below and
 * above anything it could reproduce (Ден, 2026-10-12).
 *
 * Loudness follows the equal-loudness contours, which climb steadily below
 * about 500 Hz and gently above a few kHz: its bass shelf sits at 200 Hz, so
 * the lift is already under way at 500 and nearly whole at 100; its treble
 * shelf at 7 kHz with a shallow slope, so 2 kHz gets about a decibel - that
 * region is where the ear is most sensitive already, and lifting it reads as
 * harshness rather than air. */
#define TONE_BASS_HZ 250.0f
#define TONE_TREBLE_HZ 4000.0f
#define TONE_SLOPE 0.7f
#define LOUD_BASS_HZ 200.0f
#define LOUD_BASS_SLOPE 0.7f
#define LOUD_TREBLE_HZ 7000.0f
#define LOUD_TREBLE_SLOPE 0.5f

/* Loudness at its fullest, by strength - what a forum's listeners asked for:
 * +6, +10, +15 dB of bass and +3, +6, +10 of treble, reached when the volume
 * is this far down and scaled in proportion above it. The volume curve goes
 * to -60 dB, but forty down is already a quiet room. */
#define LOUD_FULL_AT_DB 40.0f
static const float k_loud_bass_db[] = {0.0f, 6.0f, 10.0f, 15.0f};
static const float k_loud_treble_db[] = {0.0f, 3.0f, 6.0f, 10.0f};

/* How far a shelf moves per block towards where it is going: a knob turned
 * from 0 to +12 is a third of a second of slope at 1024-frame blocks, and no
 * single step is big enough to be heard as one. */
#define BAND_STEP_DB 1.0f
// Below this a shelf is off: the filter is skipped, not run as a no-op.
#define BAND_OFF_DB 0.01f

static const float k_pi = 3.14159265358979f;

void audio_tone_init(audio_tone_t *tone)
{
    if (tone == NULL) return;
    memset(tone, 0, sizeof(*tone));
}

bool audio_tone_settings_valid(const audio_tone_settings_t *settings)
{
    return settings != NULL && settings->bass_db >= AUDIO_TONE_DB_MIN &&
           settings->bass_db <= AUDIO_TONE_DB_MAX && settings->treble_db >= AUDIO_TONE_DB_MIN &&
           settings->treble_db <= AUDIO_TONE_DB_MAX && settings->loudness <= AUDIO_LOUDNESS_STRONG;
}

bool audio_tone_active(const audio_tone_settings_t *settings)
{
    return settings != NULL && (settings->bass_db != 0 || settings->treble_db != 0 ||
                                settings->loudness != AUDIO_LOUDNESS_OFF);
}

void audio_tone_set(audio_tone_t *tone, const audio_tone_settings_t *settings)
{
    if (tone == NULL || !audio_tone_settings_valid(settings)) return;
    tone->settings = *settings;
}

bool audio_tone_idle(const audio_tone_t *tone)
{
    if (tone == NULL) return true;
    if (audio_tone_active(&tone->settings)) return false;
    if (fabsf(tone->pre_db) >= BAND_OFF_DB) return false;
    for (int band = 0; band < AUDIO_TONE_BANDS; ++band) {
        if (fabsf(tone->band_db[band]) >= BAND_OFF_DB) return false;
    }
    return true;
}

static float step_towards(float now, float want, bool jump)
{
    if (jump) return want;
    if (now < want - BAND_STEP_DB) return now + BAND_STEP_DB;
    if (now > want + BAND_STEP_DB) return now - BAND_STEP_DB;
    return want;
}

void audio_tone_reset(audio_tone_t *tone)
{
    if (tone == NULL) return;
    memset(tone->state, 0, sizeof(tone->state));
}

/* How far below full scale the volume has put the sound, in dB. Silence is
 * any amount: nothing lifted out of it can reach the rail. */
static float volume_down_db(uint16_t volume_gain)
{
    if (volume_gain >= AUDIO_VOLUME_UNITY) return 0.0f;
    if (volume_gain == 0U) return 1000.0f;
    return 20.0f * log10f((float)AUDIO_VOLUME_UNITY / (float)volume_gain);
}

float audio_tone_loudness_db(uint8_t loudness, bool treble, uint16_t volume_gain)
{
    if (loudness == AUDIO_LOUDNESS_OFF || loudness > AUDIO_LOUDNESS_STRONG) return 0.0f;
    if (volume_gain >= AUDIO_VOLUME_UNITY) return 0.0f;
    const float down_db = volume_gain == 0U ? LOUD_FULL_AT_DB : volume_down_db(volume_gain);
    const float share = down_db >= LOUD_FULL_AT_DB ? 1.0f : down_db / LOUD_FULL_AT_DB;
    const float full = treble ? k_loud_treble_db[loudness] : k_loud_bass_db[loudness];
    float lift = full * share;
    /* Never more than the volume took away: what is lifted is then still
     * under full scale, with no limiter needed. */
    if (lift > down_db) lift = down_db;
    return lift;
}

/* The Audio EQ Cookbook's shelves (Robert Bristow-Johnson), normalised so
 * a0 is 1. `slope` is the cookbook's S: 1 is as steep as a shelf gets
 * without a bump, less is gentler. */
audio_biquad_t audio_tone_shelf(bool high, float freq, float slope, float gain_db,
                                uint32_t rate)
{
    audio_biquad_t out = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    if (rate == 0U || slope <= 0.0f) return out;
    // A shelf past the top of the band has nothing to stand on.
    const float top = 0.45f * (float)rate;
    if (freq > top) freq = top;
    const float a = powf(10.0f, gain_db / 40.0f);
    const float w0 = 2.0f * k_pi * freq / (float)rate;
    const float cosw = cosf(w0);
    const float alpha =
        sinf(w0) / 2.0f * sqrtf((a + 1.0f / a) * (1.0f / slope - 1.0f) + 2.0f);
    const float root = 2.0f * sqrtf(a) * alpha;
    float b0, b1, b2, a0, a1, a2;
    if (!high) {
        b0 = a * ((a + 1.0f) - (a - 1.0f) * cosw + root);
        b1 = 2.0f * a * ((a - 1.0f) - (a + 1.0f) * cosw);
        b2 = a * ((a + 1.0f) - (a - 1.0f) * cosw - root);
        a0 = (a + 1.0f) + (a - 1.0f) * cosw + root;
        a1 = -2.0f * ((a - 1.0f) + (a + 1.0f) * cosw);
        a2 = (a + 1.0f) + (a - 1.0f) * cosw - root;
    } else {
        b0 = a * ((a + 1.0f) + (a - 1.0f) * cosw + root);
        b1 = -2.0f * a * ((a - 1.0f) + (a + 1.0f) * cosw);
        b2 = a * ((a + 1.0f) + (a - 1.0f) * cosw - root);
        a0 = (a + 1.0f) - (a - 1.0f) * cosw + root;
        a1 = 2.0f * ((a - 1.0f) - (a + 1.0f) * cosw);
        a2 = (a + 1.0f) - (a - 1.0f) * cosw - root;
    }
    out.b0 = b0 / a0;
    out.b1 = b1 / a0;
    out.b2 = b2 / a0;
    out.a1 = a1 / a0;
    out.a2 = a2 / a0;
    return out;
}

float audio_tone_response_db(const audio_biquad_t *filter, float freq, uint32_t rate)
{
    if (filter == NULL || rate == 0U) return 0.0f;
    const float w = 2.0f * k_pi * freq / (float)rate;
    const float c1 = cosf(w), s1 = sinf(w), c2 = cosf(2.0f * w), s2 = sinf(2.0f * w);
    const float num_re = filter->b0 + filter->b1 * c1 + filter->b2 * c2;
    const float num_im = -(filter->b1 * s1 + filter->b2 * s2);
    const float den_re = 1.0f + filter->a1 * c1 + filter->a2 * c2;
    const float den_im = -(filter->a1 * s1 + filter->a2 * s2);
    const float num = num_re * num_re + num_im * num_im;
    const float den = den_re * den_re + den_im * den_im;
    return 10.0f * log10f(num / den);
}

/* Where each shelf should stand for this volume. */
static void band_targets(const audio_tone_settings_t *settings, uint16_t volume_gain,
                         float target[AUDIO_TONE_BANDS])
{
    target[AUDIO_TONE_BASS] = (float)settings->bass_db;
    target[AUDIO_TONE_TREBLE] = (float)settings->treble_db;
    float loud_bass = audio_tone_loudness_db(settings->loudness, false, volume_gain);
    float loud_treble = settings->loudness_treble
                            ? audio_tone_loudness_db(settings->loudness, true, volume_gain)
                            : 0.0f;
    // The two together are capped, the loudness giving way.
    const float bass_room = AUDIO_TONE_SUM_MAX_DB - (settings->bass_db > 0 ? settings->bass_db : 0);
    const float treble_room =
        AUDIO_TONE_SUM_MAX_DB - (settings->treble_db > 0 ? settings->treble_db : 0);
    if (loud_bass > bass_room) loud_bass = bass_room;
    if (loud_treble > treble_room) loud_treble = treble_room;
    target[AUDIO_TONE_LOUD_BASS] = loud_bass;
    target[AUDIO_TONE_LOUD_TREBLE] = loud_treble;
}

/* The turn-down for shelves standing at `band`: the most any frequency is
 * lifted - a tone boost and the loudness shelf at the same end add, the two
 * ends do not, and a cut lifts nothing - less what the volume has already
 * made room for. */
static float turn_down_db(const float band[AUDIO_TONE_BANDS], uint16_t volume_gain)
{
    const float bass = (band[AUDIO_TONE_BASS] > 0.0f ? band[AUDIO_TONE_BASS] : 0.0f) +
                       band[AUDIO_TONE_LOUD_BASS];
    const float treble = (band[AUDIO_TONE_TREBLE] > 0.0f ? band[AUDIO_TONE_TREBLE] : 0.0f) +
                         band[AUDIO_TONE_LOUD_TREBLE];
    const float lift = bass > treble ? bass : treble;
    const float short_by = lift - volume_down_db(volume_gain);
    return short_by > 0.0f ? short_by : 0.0f;
}

float audio_tone_headroom_db(const audio_tone_settings_t *settings, uint16_t volume_gain)
{
    if (settings == NULL) return 0.0f;
    float target[AUDIO_TONE_BANDS];
    band_targets(settings, volume_gain, target);
    return turn_down_db(target, volume_gain);
}

static audio_biquad_t design(int band, float gain_db, uint32_t rate)
{
    switch (band) {
    case AUDIO_TONE_BASS: return audio_tone_shelf(false, TONE_BASS_HZ, TONE_SLOPE, gain_db, rate);
    case AUDIO_TONE_TREBLE: return audio_tone_shelf(true, TONE_TREBLE_HZ, TONE_SLOPE, gain_db, rate);
    case AUDIO_TONE_LOUD_BASS:
        return audio_tone_shelf(false, LOUD_BASS_HZ, LOUD_BASS_SLOPE, gain_db, rate);
    default: return audio_tone_shelf(true, LOUD_TREBLE_HZ, LOUD_TREBLE_SLOPE, gain_db, rate);
    }
}

/* Each shelf a step nearer its target, and its coefficients redone when it
 * moved or the rate did. A shelf coming back from off starts from silence:
 * its memory is of whenever it last ran. */
static void move_bands(audio_tone_t *tone, uint32_t rate, uint16_t volume_gain)
{
    float target[AUDIO_TONE_BANDS];
    band_targets(&tone->settings, volume_gain, target);
    const bool rate_changed = rate != tone->rate || !tone->primed;
    if (rate_changed) {
        tone->rate = rate;
        audio_tone_reset(tone);
    }
    for (int band = 0; band < AUDIO_TONE_BANDS; ++band) {
        // A fresh start goes straight there: there is nothing to slope from.
        const float now = step_towards(tone->band_db[band], target[band], !tone->primed);
        const bool was_off = fabsf(tone->band_db[band]) < BAND_OFF_DB;
        const bool moved = now != tone->band_db[band];
        tone->band_db[band] = now;
        if (fabsf(now) < BAND_OFF_DB) continue;
        if (was_off && tone->primed) memset(tone->state[band], 0, sizeof(tone->state[band]));
        if (moved || rate_changed || !tone->primed) tone->filter[band] = design(band, now, rate);
    }
    /* From where the shelves stand now, not where they are going: they
     * slope, and a turn-down taken in full at their first step would dip the
     * sound before the boost had arrived. Down at once - within the block,
     * as process() ramps it - since a turn-down that lagged would let the
     * lift reach the rail; back up a step at a time, so the sound does not
     * jump louder. */
    const float pre_target = -turn_down_db(tone->band_db, volume_gain);
    tone->pre_db = step_towards(tone->pre_db, pre_target,
                                !tone->primed || pre_target < tone->pre_db);
    tone->primed = true;
}

/* One shelf over a chunk, in the transposed direct form II: two numbers of
 * state per channel, and the five coefficients and all four states held in
 * registers for the whole chunk rather than fetched per sample - the
 * per-sample cascade this replaced cost a sixth of a core at 44.1 kHz. Both
 * channels in the one loop, since their two chains do not wait on each
 * other and the FPU can overlap them. In float the form's rounding sits far
 * below 16 bits. */
static void run_chunk(const audio_biquad_t *f, audio_biquad_state_t s[2], float *x, size_t frames)
{
    const float b0 = f->b0, b1 = f->b1, b2 = f->b2, a1 = f->a1, a2 = f->a2;
    float l1 = s[0].z1, l2 = s[0].z2, r1 = s[1].z1, r2 = s[1].z2;
    for (size_t i = 0U; i < frames; ++i) {
        const float left = x[i * 2U];
        const float right = x[i * 2U + 1U];
        const float yl = b0 * left + l1;
        const float yr = b0 * right + r1;
        l1 = b1 * left - a1 * yl + l2;
        r1 = b1 * right - a1 * yr + r2;
        l2 = b2 * left - a2 * yl;
        r2 = b2 * right - a2 * yr;
        x[i * 2U] = yl;
        x[i * 2U + 1U] = yr;
    }
    s[0].z1 = l1;
    s[0].z2 = l2;
    s[1].z1 = r1;
    s[1].z2 = r2;
}

// A state this small is silence; kept, it would be a denormal to grind on.
static void flush_tiny(audio_biquad_state_t *s)
{
    const float tiny = 1e-12f;
    if (fabsf(s->z1) < tiny) s->z1 = 0.0f;
    if (fabsf(s->z2) < tiny) s->z2 = 0.0f;
}

static int16_t to_sample(float v)
{
    v += v >= 0.0f ? 0.5f : -0.5f;
    if (v > 32767.0f) return 32767;
    if (v < -32768.0f) return -32768;
    return (int16_t)v;
}

/* Frames per pass through the shelves: small enough to sit on the caller's
 * stack (256 bytes), big enough that loading a shelf's coefficients is paid
 * once per chunk rather than per sample. */
#define CHUNK_FRAMES 32U

void audio_tone_process(audio_tone_t *tone, uint32_t rate, const int16_t *in, int16_t *out,
                        size_t frames, uint16_t gain_start, uint16_t gain_end)
{
    if (tone == NULL || in == NULL || out == NULL || frames == 0U) return;
    const bool was_primed = tone->primed;
    const float pre_was = tone->pre_db;
    move_bands(tone, rate, gain_end);
    const float pre_before = was_primed ? pre_was : tone->pre_db;
    /* The tone boosts paid for up front, on top of the volume - and moved
     * across the block with it, so the turn-down's 1 dB per block is a slope
     * and not a step at the block's edge. */
    const float pre_start = powf(10.0f, pre_before / 20.0f) / (float)AUDIO_VOLUME_UNITY;
    const float pre_end = powf(10.0f, tone->pre_db / 20.0f) / (float)AUDIO_VOLUME_UNITY;
    bool on[AUDIO_TONE_BANDS];
    for (int band = 0; band < AUDIO_TONE_BANDS; ++band) {
        on[band] = fabsf(tone->band_db[band]) >= BAND_OFF_DB;
    }
    float gain = (float)gain_start * pre_start;
    const float gain_step = ((float)gain_end * pre_end - gain) / (float)frames;
    float chunk[CHUNK_FRAMES * 2U];
    for (size_t at = 0U; at < frames; at += CHUNK_FRAMES) {
        const size_t n = frames - at < CHUNK_FRAMES ? frames - at : CHUNK_FRAMES;
        const int16_t *src = in + at * 2U;
        for (size_t i = 0U; i < n; ++i) {
            chunk[i * 2U] = (float)src[i * 2U] * gain;
            chunk[i * 2U + 1U] = (float)src[i * 2U + 1U] * gain;
            gain += gain_step;
        }
        for (int band = 0; band < AUDIO_TONE_BANDS; ++band) {
            if (!on[band]) continue;
            run_chunk(&tone->filter[band], tone->state[band], chunk, n);
        }
        int16_t *dst = out + at * 2U;
        for (size_t i = 0U; i < n * 2U; ++i) dst[i] = to_sample(chunk[i]);
    }
    for (int band = 0; band < AUDIO_TONE_BANDS; ++band) {
        flush_tiny(&tone->state[band][0]);
        flush_tiny(&tone->state[band][1]);
    }
}
