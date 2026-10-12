#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_tone.h"
#include "audio_volume.h"

#define RATE 44100U
#define NEAR(value, want, tolerance) (fabsf((value) - (want)) <= (tolerance))

static void test_the_shelves_lift_where_they_should(void)
{
    // A bass shelf: the lift whole at the bottom, half at its frequency, none above.
    const audio_biquad_t bass = audio_tone_shelf(false, 100.0f, 1.0f, 12.0f, RATE);
    assert(NEAR(audio_tone_response_db(&bass, 20.0f, RATE), 12.0f, 0.6f));
    assert(NEAR(audio_tone_response_db(&bass, 100.0f, RATE), 6.0f, 0.6f));
    assert(NEAR(audio_tone_response_db(&bass, 5000.0f, RATE), 0.0f, 0.1f));
    // And a cut is the mirror of a lift.
    const audio_biquad_t cut = audio_tone_shelf(false, 100.0f, 1.0f, -12.0f, RATE);
    assert(NEAR(audio_tone_response_db(&cut, 20.0f, RATE), -12.0f, 0.6f));
    // A treble shelf the other way up.
    const audio_biquad_t treble = audio_tone_shelf(true, 10000.0f, 1.0f, 6.0f, RATE);
    assert(NEAR(audio_tone_response_db(&treble, 18000.0f, RATE), 6.0f, 1.0f));
    assert(NEAR(audio_tone_response_db(&treble, 1000.0f, RATE), 0.0f, 0.2f));
    // The loudness treble shelf barely touches 2 kHz, where the ear is keenest.
    const audio_biquad_t loud = audio_tone_shelf(true, 7000.0f, 0.5f, 10.0f, RATE);
    assert(audio_tone_response_db(&loud, 2000.0f, RATE) < 2.0f);
    assert(audio_tone_response_db(&loud, 15000.0f, RATE) > 7.0f);
    // A shelf past the top of a low rate stands below it instead.
    const audio_biquad_t top = audio_tone_shelf(true, 10000.0f, 1.0f, 6.0f, 16000U);
    assert(isfinite(top.b0) && isfinite(top.a1));
}

static void test_loudness_follows_the_volume(void)
{
    // Nothing at full volume, whatever the strength.
    assert(audio_tone_loudness_db(AUDIO_LOUDNESS_STRONG, false, AUDIO_VOLUME_UNITY) == 0.0f);
    assert(audio_tone_loudness_db(AUDIO_LOUDNESS_OFF, false, 100U) == 0.0f);
    // At the bottom, the strengths in full: +6/+10/+15 bass, +3/+6/+10 treble.
    const uint16_t bottom = audio_volume_gain(1U);  // -60 dB
    assert(NEAR(audio_tone_loudness_db(AUDIO_LOUDNESS_WEAK, false, bottom), 6.0f, 0.01f));
    assert(NEAR(audio_tone_loudness_db(AUDIO_LOUDNESS_MEDIUM, false, bottom), 10.0f, 0.01f));
    assert(NEAR(audio_tone_loudness_db(AUDIO_LOUDNESS_STRONG, false, bottom), 15.0f, 0.01f));
    assert(NEAR(audio_tone_loudness_db(AUDIO_LOUDNESS_STRONG, true, bottom), 10.0f, 0.01f));
    // Half way down the forty decibels, half the lift.
    const uint16_t minus20 = audio_volume_gain(50U);  // -20 dB
    assert(NEAR(audio_tone_loudness_db(AUDIO_LOUDNESS_MEDIUM, false, minus20), 5.0f, 0.1f));
    // Never more than the volume took away.
    for (unsigned percent = 1U; percent <= 100U; ++percent) {
        const uint16_t gain = audio_volume_gain((uint8_t)percent);
        const float down = gain >= AUDIO_VOLUME_UNITY
                               ? 0.0f
                               : 20.0f * log10f((float)AUDIO_VOLUME_UNITY / (float)gain);
        assert(audio_tone_loudness_db(AUDIO_LOUDNESS_STRONG, false, gain) <= down + 0.001f);
    }
}

/* The tone shelves lift what a small speaker plays: at +12, most of it by
 * 150 Hz and by 6 kHz, and the middle left alone. Hinged at 100 Hz and 10 kHz
 * they lifted 2 dB at 150 Hz and 1 dB at 6 kHz, and +12 was barely heard. */
static void test_the_tone_shelves_act_where_a_speaker_plays(void)
{
    audio_tone_t tone;
    audio_tone_init(&tone);
    const audio_tone_settings_t both = {.bass_db = 12, .treble_db = 12};
    audio_tone_set(&tone, &both);
    int16_t block[64] = {0};
    audio_tone_process(&tone, RATE, block, block, 32U, AUDIO_VOLUME_UNITY, AUDIO_VOLUME_UNITY);
    const audio_biquad_t *bass = &tone.filter[AUDIO_TONE_BASS];
    const audio_biquad_t *treble = &tone.filter[AUDIO_TONE_TREBLE];
    assert(audio_tone_response_db(bass, 150.0f, RATE) > 8.0f);
    assert(audio_tone_response_db(treble, 6000.0f, RATE) > 8.0f);
    assert(audio_tone_response_db(bass, 1500.0f, RATE) < 0.5f);
    assert(audio_tone_response_db(treble, 700.0f, RATE) < 0.5f);
}

static void test_settings_and_headroom(void)
{
    audio_tone_settings_t flat = {0};
    assert(!audio_tone_active(&flat));
    assert(audio_tone_settings_valid(&flat));
    audio_tone_settings_t tone = {.bass_db = 8, .treble_db = 3};
    assert(audio_tone_active(&tone));
    // At full volume the largest boost is paid for in full...
    assert(audio_tone_headroom_db(&tone, AUDIO_VOLUME_UNITY) == 8.0f);
    tone = (audio_tone_settings_t){.bass_db = -6, .treble_db = 4};
    assert(audio_tone_headroom_db(&tone, AUDIO_VOLUME_UNITY) == 4.0f);
    // A cut needs no headroom.
    tone = (audio_tone_settings_t){.bass_db = -6};
    assert(audio_tone_headroom_db(&tone, AUDIO_VOLUME_UNITY) == 0.0f);
    /* ...and below it only what the volume has not made room for: at -20 dB
     * a +8 boost fits, and a boost is then just a boost. */
    tone = (audio_tone_settings_t){.bass_db = 8, .treble_db = 3};
    const uint16_t minus20 = audio_volume_gain(50U);
    assert(audio_tone_headroom_db(&tone, minus20) == 0.0f);
    // Near the top, the part that does not fit.
    const float down = 20.0f * log10f((float)AUDIO_VOLUME_UNITY / (float)audio_volume_gain(95U));
    assert(NEAR(audio_tone_headroom_db(&tone, audio_volume_gain(95U)), 8.0f - down, 0.01f));
    /* Tone and loudness at the same end add: +12 of bass and the strongest
     * loudness at the bottom is 18 dB of lift, which -60 dB has room for. */
    tone = (audio_tone_settings_t){.bass_db = 12, .loudness = AUDIO_LOUDNESS_STRONG};
    assert(audio_tone_headroom_db(&tone, audio_volume_gain(1U)) == 0.0f);
    tone = (audio_tone_settings_t){.loudness = AUDIO_LOUDNESS_WEAK};
    assert(audio_tone_active(&tone));
    tone = (audio_tone_settings_t){.bass_db = 13};
    assert(!audio_tone_settings_valid(&tone));
    tone = (audio_tone_settings_t){.loudness = 4};
    assert(!audio_tone_settings_valid(&tone));
}

static int16_t *sine(size_t frames, float freq, float amplitude)
{
    int16_t *samples = malloc(frames * 2U * sizeof(int16_t));
    assert(samples != NULL);
    for (size_t i = 0U; i < frames; ++i) {
        const int16_t v = (int16_t)lrintf(amplitude * sinf(2.0f * 3.14159265f * freq * (float)i /
                                                             (float)RATE));
        samples[i * 2U] = v;
        samples[i * 2U + 1U] = v;
    }
    return samples;
}

static void test_flat_at_full_volume_is_bit_exact(void)
{
    audio_tone_t tone;
    audio_tone_init(&tone);
    audio_tone_settings_t flat = {0};
    audio_tone_set(&tone, &flat);
    int16_t *in = sine(4096U, 997.0f, 30000.0f);
    in[10] = 32767;
    in[11] = -32768;
    int16_t *out = malloc(4096U * 2U * sizeof(int16_t));
    audio_tone_process(&tone, RATE, in, out, 4096U, AUDIO_VOLUME_UNITY, AUDIO_VOLUME_UNITY);
    assert(memcmp(in, out, 4096U * 2U * sizeof(int16_t)) == 0);
    free(in);
    free(out);
}

/* The headroom rule, heard: a full-scale low note through +12 dB of bass at
 * full volume, and through the strongest loudness at the bottom of the scale,
 * comes out whole - no sample pinned at the rail. */
static void test_nothing_clips(void)
{
    const size_t frames = 44100U;
    int16_t *in = sine(frames, 60.0f, 32000.0f);
    int16_t *out = malloc(frames * 2U * sizeof(int16_t));
    audio_tone_t tone;
    audio_tone_init(&tone);
    audio_tone_settings_t boost = {.bass_db = 12, .treble_db = 12};
    audio_tone_set(&tone, &boost);
    for (size_t at = 0U; at < frames; at += 1024U) {
        const size_t n = frames - at < 1024U ? frames - at : 1024U;
        audio_tone_process(&tone, RATE, in + at * 2U, out + at * 2U, n, AUDIO_VOLUME_UNITY,
                           AUDIO_VOLUME_UNITY);
    }
    int peak = 0;
    for (size_t i = 0U; i < frames * 2U; ++i) {
        const int v = abs(out[i]);
        if (v > peak) peak = v;
    }
    assert(peak < 32767);
    // And the bass really is up: the 60 Hz note lost little of the 12 dB taken off first.
    assert(peak > 32000 / 2);

    audio_tone_init(&tone);
    audio_tone_settings_t loud = {.loudness = AUDIO_LOUDNESS_STRONG, .loudness_treble = true};
    audio_tone_set(&tone, &loud);
    const uint16_t bottom = audio_volume_gain(1U);
    for (size_t at = 0U; at < frames; at += 1024U) {
        const size_t n = frames - at < 1024U ? frames - at : 1024U;
        audio_tone_process(&tone, RATE, in + at * 2U, out + at * 2U, n, bottom, bottom);
    }
    peak = 0;
    for (size_t i = frames; i < frames * 2U; ++i) {
        const int v = abs(out[i]);
        if (v > peak) peak = v;
    }
    // -60 dB of volume and up to +15 dB of lift: about -45 dBFS, far from the rail.
    assert(peak > 0 && peak < 32767 / 50);
    free(in);
    free(out);
}

static int peak_after(const audio_tone_settings_t *settings, float freq, uint16_t gain)
{
    const size_t frames = 22050U;
    int16_t *in = sine(frames, freq, 16000.0f);
    int16_t *out = malloc(frames * 2U * sizeof(int16_t));
    audio_tone_t tone;
    audio_tone_init(&tone);
    audio_tone_set(&tone, settings);
    for (size_t at = 0U; at < frames; at += 1024U) {
        const size_t n = frames - at < 1024U ? frames - at : 1024U;
        audio_tone_process(&tone, RATE, in + at * 2U, out + at * 2U, n, gain, gain);
    }
    int peak = 0;
    for (size_t i = frames; i < frames * 2U; ++i) {
        if (abs(out[i]) > peak) peak = abs(out[i]);
    }
    free(in);
    free(out);
    return peak;
}

/* What Ден heard as "I turn it up and it gets quieter": below full volume a
 * bass boost now lifts the bass and leaves the middle where it was. */
static void test_a_boost_below_full_volume_is_a_boost(void)
{
    const uint16_t half = audio_volume_gain(50U);  // -20 dB
    const audio_tone_settings_t flat = {0};
    const audio_tone_settings_t bass = {.bass_db = 6};
    const float low_gain = 20.0f * log10f((float)peak_after(&bass, 40.0f, half) /
                                          (float)peak_after(&flat, 40.0f, half));
    const float mid_gain = 20.0f * log10f((float)peak_after(&bass, 1000.0f, half) /
                                          (float)peak_after(&flat, 1000.0f, half));
    assert(low_gain > 5.0f && low_gain < 6.5f);
    assert(fabsf(mid_gain) < 0.5f);
    // At full volume the middle still pays for it, so nothing clips.
    const float top_mid = 20.0f * log10f((float)peak_after(&bass, 1000.0f, AUDIO_VOLUME_UNITY) /
                                         (float)peak_after(&flat, 1000.0f, AUDIO_VOLUME_UNITY));
    assert(top_mid < -5.0f);
}

/* A knob turned is a slope a decibel a block, not a step; the sum is capped
 * with the loudness giving way. */
static void test_slopes_and_the_cap(void)
{
    audio_tone_t tone;
    audio_tone_init(&tone);
    audio_tone_settings_t flat = {0};
    audio_tone_set(&tone, &flat);
    int16_t block[2048] = {0};
    audio_tone_process(&tone, RATE, block, block, 1024U, AUDIO_VOLUME_UNITY, AUDIO_VOLUME_UNITY);
    audio_tone_settings_t up = {.bass_db = 12};
    audio_tone_set(&tone, &up);
    audio_tone_process(&tone, RATE, block, block, 1024U, AUDIO_VOLUME_UNITY, AUDIO_VOLUME_UNITY);
    assert(NEAR(tone.band_db[AUDIO_TONE_BASS], 1.0f, 0.001f));
    for (int i = 0; i < 20; ++i) {
        audio_tone_process(&tone, RATE, block, block, 1024U, AUDIO_VOLUME_UNITY,
                           AUDIO_VOLUME_UNITY);
    }
    assert(NEAR(tone.band_db[AUDIO_TONE_BASS], 12.0f, 0.001f));
    assert(NEAR(tone.pre_db, -12.0f, 0.001f));
    assert(!audio_tone_idle(&tone));
    // Switched off, it slopes back down and only then is idle.
    audio_tone_set(&tone, &flat);
    audio_tone_process(&tone, RATE, block, block, 1024U, AUDIO_VOLUME_UNITY, AUDIO_VOLUME_UNITY);
    assert(NEAR(tone.band_db[AUDIO_TONE_BASS], 11.0f, 0.001f));
    assert(NEAR(tone.pre_db, -11.0f, 0.001f));
    assert(!audio_tone_idle(&tone));
    for (int i = 0; i < 20; ++i) {
        audio_tone_process(&tone, RATE, block, block, 1024U, AUDIO_VOLUME_UNITY,
                           AUDIO_VOLUME_UNITY);
    }
    assert(audio_tone_idle(&tone));

    // +12 of bass and strong loudness at the bottom: 18 in all, loudness 6.
    audio_tone_settings_t both = {.bass_db = 12, .loudness = AUDIO_LOUDNESS_STRONG};
    audio_tone_set(&tone, &both);
    const uint16_t bottom = audio_volume_gain(1U);
    for (int i = 0; i < 30; ++i) {
        audio_tone_process(&tone, RATE, block, block, 1024U, bottom, bottom);
    }
    assert(NEAR(tone.band_db[AUDIO_TONE_LOUD_BASS], 6.0f, 0.001f));
    // A new rate is a fresh start: its filters have no memory of the old one.
    tone.state[AUDIO_TONE_BASS][0].z1 = 1234.0f;
    audio_tone_process(&tone, 48000U, block, block, 16U, bottom, bottom);
    assert(tone.rate == 48000U);
    assert(fabsf(tone.state[AUDIO_TONE_BASS][0].z1) < 1.0f);
}

int main(void)
{
    test_the_shelves_lift_where_they_should();
    test_loudness_follows_the_volume();
    test_the_tone_shelves_act_where_a_speaker_plays();
    test_settings_and_headroom();
    test_flat_at_full_volume_is_bit_exact();
    test_nothing_clips();
    test_a_boost_below_full_volume_is_a_boost();
    test_slopes_and_the_cap();
    printf("audio tone tests passed\n");
    return 0;
}
