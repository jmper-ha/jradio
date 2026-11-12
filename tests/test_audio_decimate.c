#include "audio_decimate.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

enum { RATE = 96000, FRAMES = 9600 };

static void sine(int16_t *pcm, size_t frames, double hz, double amplitude)
{
    for (size_t frame = 0; frame < frames; ++frame) {
        const double value = amplitude * sin(2.0 * M_PI * hz * (double)frame / RATE);
        pcm[2U * frame] = (int16_t)lrint(value);
        pcm[2U * frame + 1U] = (int16_t)lrint(-value);
    }
}

// The output's RMS past the filter's settling, per channel.
static double rms(const int16_t *pcm, size_t frames, unsigned channel)
{
    double sum = 0.0;
    size_t count = 0U;
    for (size_t frame = 64U; frame < frames; ++frame, ++count) {
        sum += (double)pcm[2U * frame + channel] * pcm[2U * frame + channel];
    }
    return sqrt(sum / (double)count);
}

static size_t run(unsigned factor, const int16_t *in, size_t frames, int16_t *out)
{
    audio_decimate_t decimate;
    audio_decimate_init(&decimate, factor);
    return audio_decimate_s16(&decimate, in, frames, out);
}

static void test_the_factor_brings_the_rate_to_48k_or_under(void)
{
    assert(audio_decimate_factor(44100U) == 1U);
    assert(audio_decimate_factor(48000U) == 1U);
    assert(audio_decimate_factor(88200U) == 2U);
    assert(audio_decimate_factor(96000U) == 2U);
    assert(audio_decimate_factor(176400U) == 4U);
    assert(audio_decimate_factor(192000U) == 4U);
}

static void test_the_audible_band_passes_and_the_folding_one_does_not(void)
{
    static int16_t in[2U * FRAMES];
    static int16_t out[2U * FRAMES];
    const double amplitude = 20000.0;
    const double full = amplitude / sqrt(2.0);
    sine(in, FRAMES, 1000.0, amplitude);
    assert(run(2U, in, FRAMES, out) == FRAMES / 2U);
    assert(fabs(rms(out, FRAMES / 2U, 0U) / full - 1.0) < 0.01);
    assert(fabs(rms(out, FRAMES / 2U, 1U) / full - 1.0) < 0.01);
    sine(in, FRAMES, 19000.0, amplitude);
    run(2U, in, FRAMES, out);
    assert(fabs(rms(out, FRAMES / 2U, 0U) / full - 1.0) < 0.02);
    // 30 kHz would land on 18 kHz at 48: it must be gone, 70 dB at least.
    sine(in, FRAMES, 30000.0, amplitude);
    run(2U, in, FRAMES, out);
    assert(20.0 * log10(rms(out, FRAMES / 2U, 0U) / full + 1e-12) < -70.0);
}

static void test_four_to_one_through_two_stages(void)
{
    static int16_t in[2U * FRAMES];
    static int16_t out[2U * FRAMES];
    // As 192 kHz: 1 kHz here is 2 kHz there, still in the band.
    sine(in, FRAMES, 1000.0, 20000.0);
    assert(run(4U, in, FRAMES, out) == FRAMES / 4U);
    assert(fabs(rms(out, FRAMES / 4U, 0U) / (20000.0 / sqrt(2.0)) - 1.0) < 0.01);
}

static void test_a_block_split_anywhere_gives_the_same_samples(void)
{
    static int16_t in[2U * FRAMES];
    static int16_t whole[2U * FRAMES];
    static int16_t split[2U * FRAMES];
    sine(in, FRAMES, 5000.0, 30000.0);
    const size_t expected = run(4U, in, FRAMES, whole);
    audio_decimate_t decimate;
    audio_decimate_init(&decimate, 4U);
    size_t done = 0U;
    size_t produced = 0U;
    for (size_t step = 1U; done < FRAMES; step = step % 7U + 1U) {
        const size_t take = step < FRAMES - done ? step : FRAMES - done;
        // n x factor in, never more than n out - what the ring's room relies on.
        const size_t got = audio_decimate_s16(&decimate, &in[2U * done], take, &split[2U * produced]);
        assert(got <= take / 4U + 1U);
        done += take;
        produced += got;
    }
    assert(produced == expected);
    assert(memcmp(whole, split, expected * 2U * sizeof(int16_t)) == 0);
}

static void test_factor_one_is_a_copy(void)
{
    const int16_t in[6] = {1, -2, 3, -4, 32767, -32768};
    int16_t out[6] = {0};
    assert(run(1U, in, 3U, out) == 3U);
    assert(memcmp(in, out, sizeof(in)) == 0);
}

int main(void)
{
    test_the_factor_brings_the_rate_to_48k_or_under();
    test_the_audible_band_passes_and_the_folding_one_does_not();
    test_four_to_one_through_two_stages();
    test_a_block_split_anywhere_gives_the_same_samples();
    test_factor_one_is_a_copy();
    return 0;
}
