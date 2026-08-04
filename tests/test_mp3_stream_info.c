#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "mp3_stream_info.h"

static void test_mpeg1_layer3_bitrate(void)
{
    const uint8_t frame[] = {0xff, 0xfb, 0xd2, 0x40};
    assert(mp3_stream_bitrate_kbps(frame, sizeof(frame)) == 256U);
}

static void test_mpeg2_layer3_bitrate(void)
{
    const uint8_t frame[] = {0xff, 0xf3, 0x80, 0x00};
    assert(mp3_stream_bitrate_kbps(frame, sizeof(frame)) == 64U);
}

static void test_invalid_frame_has_no_bitrate(void)
{
    const uint8_t invalid[] = {0xff, 0xfb, 0xf0, 0x00};
    const uint8_t reserved_sample_rate[] = {0xff, 0xfb, 0xde, 0x40};
    assert(mp3_stream_bitrate_kbps(invalid, sizeof(invalid)) == 0U);
    assert(mp3_stream_bitrate_kbps(reserved_sample_rate, sizeof(reserved_sample_rate)) == 0U);
    assert(mp3_stream_bitrate_kbps(NULL, 0U) == 0U);
}

int main(void)
{
    test_mpeg1_layer3_bitrate();
    test_mpeg2_layer3_bitrate();
    test_invalid_frame_has_no_bitrate();
    puts("mp3_stream_info tests passed");
    return 0;
}
