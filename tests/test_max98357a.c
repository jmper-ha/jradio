#include "max98357a.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static void test_rates(void)
{
    const unsigned rates[] = {8000,11025,12000,16000,22050,24000,32000,44100,48000,
                              64000,88200,96000,128000,176400,192000};
    const unsigned bus[] = {32000,44100,48000,32000,44100,48000,32000,44100,48000,
                            32000,44100,48000,32000,44100,48000};
    for (size_t i = 0; i < sizeof(rates)/sizeof(*rates); ++i) {
        max98357a_rate_t p;
        assert(max98357a_rate_plan(rates[i], &p));
        assert(p.bus_hz == bus[i]);
        assert(rates[i] * p.up == bus[i] * p.down);
    }
    max98357a_rate_t p = {123,1,1};
    assert(!max98357a_rate_plan(0, &p));
    assert(!max98357a_rate_plan(14000, &p));
    assert(!max98357a_rate_plan(384000, &p));
    assert(p.bus_hz == 123);
    assert(max98357a_output_capacity(SIZE_MAX - 3, 4) == 0);
    assert(max98357a_output_capacity(3, 1) == 0);
    assert(max98357a_ceiling_gain(-12) == 8230);
    assert(max98357a_ceiling_gain(0) == 32768);
    assert(max98357a_ceiling_gain(1) == 0);
    assert(max98357a_ceiling_gain(-61) == 0);
}

/* Sink takes arbitrary byte counts (even part of a frame), occasionally no
 * bytes. The next source block must not overwrite any retained output. */
static size_t convert(unsigned rate, const int16_t *in, size_t frames,
                       bool split, int16_t *out, uint16_t gain)
{
    max98357a_rate_t p;
    assert(max98357a_rate_plan(rate, &p));
    max98357a_output_t o;
    max98357a_output_reset(&o, p, gain);
    uint8_t buffer[32768];
    size_t done = 0, made = 0, turn = 0;
    while (done < frames) {
        size_t count = split ? 1 + (done * 7) % 17 : frames;
        if (count > frames - done) count = frames - done;
        assert(max98357a_output_prepare(&o, (const uint8_t *)(in + done * 2),
                                          count * 4, buffer, sizeof(buffer)));
        done += count;
        while (o.pending_bytes) {
            assert(!max98357a_output_prepare(&o, (const uint8_t *)in, 4,
                                               buffer, sizeof(buffer)));
            assert(max98357a_output_advance(&o, 0)); // timeout, no progress
            size_t take = split ? 1 + (++turn * 13) % 31 : o.pending_bytes;
            if (take > o.pending_bytes) take = o.pending_bytes;
            memcpy((uint8_t *)out + made, buffer + o.pending_at, take);
            made += take;
            assert(max98357a_output_advance(&o, take));
        }
    }
    assert(!max98357a_output_advance(&o, 1));
    return made;
}

static void test_streaming(void)
{
    int16_t input[514], whole[2056], split[2056];
    for (size_t i = 0; i < 257; ++i) {
        input[2*i] = (int16_t)((i * 997) % 32768);
        input[2*i+1] = (int16_t)-(int)input[2*i];
    }
    input[0] = 32767; input[1] = -32768;
    const unsigned rates[] = {8000,11025,12000,16000,22050,24000,32000,44100,48000,
                              64000,88200,96000,128000,176400,192000};
    for (size_t i = 0; i < sizeof(rates)/sizeof(*rates); ++i) {
        size_t n = convert(rates[i], input, 257, false, whole, 8230);
        assert(convert(rates[i], input, 257, true, split, 8230) == n);
        assert(memcmp(whole, split, n) == 0);
        for (size_t j = 0; j < n/2; ++j) assert(split[j] >= -8230 && split[j] <= 8230);
    }
    assert(convert(44100, input, 257, true, split, 32768) == sizeof(input));
    assert(memcmp(input, split, sizeof(input)) == 0);
    max98357a_output_t o, before;
    max98357a_rate_t p;
    assert(max98357a_rate_plan(22050, &p));
    max98357a_output_reset(&o, p, 8230); before = o;
    assert(!max98357a_output_prepare(&o, (uint8_t *)input, 16, (uint8_t *)split, 8));
    assert(memcmp(&o, &before, sizeof(o)) == 0);
    assert(max98357a_output_prepare(&o, (uint8_t *)input, 16, (uint8_t *)split, sizeof(split)));
    max98357a_output_reset(&o, p, 8230);
    assert(!o.pending_bytes && o.previous[0] == 0 && o.previous[1] == 0);
}

static void test_tone_duration_and_channels(void)
{
    int16_t input[4800], output[9600];
    const unsigned sources[] = {22050, 24000};
    for (size_t k = 0; k < 2; ++k) {
        const unsigned rate = sources[k], frames = rate / 10;
        for (unsigned i = 0; i < frames; ++i) {
            input[2*i] = (int16_t)(20000 * sin(6.283185307179586 * 1000 * i / rate));
            input[2*i+1] = 0;
        }
        size_t bytes = convert(rate, input, frames, false, output, 32768);
        assert(bytes == frames * 8); // still 100 ms, twice as many frames
        unsigned crossings = 0;
        for (size_t i = 1; i < bytes / 4; ++i) {
            assert(output[2*i+1] == 0);
            if (output[2*(i-1)] <= 0 && output[2*i] > 0) ++crossings;
        }
        assert(crossings >= 99 && crossings <= 100); // still 1 kHz
    }
}

int main(void)
{
    test_rates();
    test_streaming();
    test_tone_duration_and_channels();
    puts("max98357a tests passed");
}
