#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "audio_pcm_convert.h"

/* Guard bytes around every destination, so a conversion that writes one byte
 * past its own output is caught here rather than as corruption of whatever the
 * decoder allocated next to its PCM buffer. */
#define GUARD 0xA5U

static void fill_guarded(uint8_t *buffer, size_t size)
{
    memset(buffer, GUARD, size);
}

static void assert_guard_intact(const uint8_t *buffer, size_t from, size_t to)
{
    for (size_t index = from; index < to; ++index) {
        assert(buffer[index] == GUARD);
    }
}

static void test_mono_is_duplicated_into_both_channels(void)
{
    /* The whole point: one mono sample becomes the same value in left and
     * right, so the frame count is preserved and the track plays at its own
     * speed rather than twice it. */
    const uint8_t source[] = {0x34, 0x12, 0x00, 0x80, 0xff, 0x7f};
    uint8_t destination[32];
    fill_guarded(destination, sizeof(destination));
    size_t written = 0U;

    assert(audio_pcm_to_stereo_s16(source, sizeof(source), 1U, destination,
                                   sizeof(destination), &written));
    assert(written == sizeof(source) * 2U);

    const uint8_t expected[] = {0x34, 0x12, 0x34, 0x12, 0x00, 0x80,
                                0x00, 0x80, 0xff, 0x7f, 0xff, 0x7f};
    assert(memcmp(destination, expected, sizeof(expected)) == 0);
    assert_guard_intact(destination, sizeof(expected), sizeof(destination));
}

static void test_stereo_passes_through_unchanged(void)
{
    /* Stereo is the common case and must cost nothing but the copy; a sample
     * altered here would be audible on every station. */
    const uint8_t source[] = {0x01, 0x02, 0x03, 0x04, 0xfe, 0xff, 0x00, 0x80};
    uint8_t destination[16];
    fill_guarded(destination, sizeof(destination));
    size_t written = 0U;

    assert(audio_pcm_to_stereo_s16(source, sizeof(source), 2U, destination,
                                   sizeof(destination), &written));
    assert(written == sizeof(source));
    assert(memcmp(destination, source, sizeof(source)) == 0);
    assert_guard_intact(destination, sizeof(source), sizeof(destination));
}

static void test_a_full_mp3_frame_fits_the_stereo_buffer(void)
{
    /* 1152 frames is one MP3 granule pair, the largest block the decoder
     * hands over. Mono expands to exactly the size stereo would have been,
     * which is why a buffer sized for stereo never has to grow for mono. */
    static uint8_t source[1152U * 2U];
    static uint8_t destination[1152U * 2U * 2U];
    for (size_t index = 0U; index < sizeof(source); ++index) {
        source[index] = (uint8_t)index;
    }
    size_t written = 0U;

    assert(audio_pcm_to_stereo_s16(source, sizeof(source), 1U, destination,
                                   sizeof(destination), &written));
    assert(written == sizeof(destination));
    assert(audio_pcm_stereo_bytes(sizeof(source), 1U) == sizeof(destination));

    for (size_t frame = 0U; frame < 1152U; ++frame) {
        assert(destination[frame * 4U] == source[frame * 2U]);
        assert(destination[frame * 4U + 1U] == source[frame * 2U + 1U]);
        assert(destination[frame * 4U + 2U] == source[frame * 2U]);
        assert(destination[frame * 4U + 3U] == source[frame * 2U + 1U]);
    }
}

static void test_a_destination_one_byte_short_is_refused_untouched(void)
{
    /* Writing as much as fits would put a burst of noise on the output and
     * swap the channels for everything after it, so a short buffer has to
     * fail whole. */
    const uint8_t source[] = {0x11, 0x22, 0x33, 0x44};
    uint8_t destination[8];
    size_t written = 12345U;

    fill_guarded(destination, sizeof(destination));
    assert(!audio_pcm_to_stereo_s16(source, sizeof(source), 1U, destination,
                                    sizeof(destination) - 1U, &written));
    assert_guard_intact(destination, 0U, sizeof(destination));
    assert(written == 12345U);

    /* Exactly enough succeeds, which pins the boundary rather than just the
     * failure. */
    assert(audio_pcm_to_stereo_s16(source, sizeof(source), 1U, destination,
                                   sizeof(destination), &written));
    assert(written == sizeof(destination));
}

static void test_a_partial_frame_is_refused(void)
{
    /* Half a sample, or an odd sample in a stereo block, means the caller has
     * already lost frame alignment. */
    const uint8_t source[] = {0x11, 0x22, 0x33};
    uint8_t destination[16];
    size_t written = 0U;

    assert(!audio_pcm_to_stereo_s16(source, 3U, 1U, destination, sizeof(destination),
                                    &written));
    assert(!audio_pcm_to_stereo_s16(source, 2U, 2U, destination, sizeof(destination),
                                    &written));
    assert(audio_pcm_stereo_bytes(3U, 1U) == 0U);
    assert(audio_pcm_stereo_bytes(2U, 2U) == 0U);
}

static void test_an_empty_block_is_not_an_error(void)
{
    /* A decoder can legitimately return zero bytes - an MP3 Xing header frame
     * carries no audio - and that must not look like a failure. */
    const uint8_t source[] = {0x00};
    uint8_t destination[4];
    size_t written = 12345U;

    assert(audio_pcm_to_stereo_s16(source, 0U, 1U, destination, sizeof(destination),
                                   &written));
    assert(written == 0U);
    assert(audio_pcm_stereo_bytes(0U, 2U) == 0U);
}

static void test_an_unsupported_channel_count_is_refused(void)
{
    /* Neither decoder can produce these, so the only way one arrives is a
     * caller reading the count from the wrong place. */
    const uint8_t source[] = {0x11, 0x22, 0x33, 0x44};
    uint8_t destination[16];
    size_t written = 0U;

    assert(!audio_pcm_to_stereo_s16(source, sizeof(source), 0U, destination,
                                    sizeof(destination), &written));
    assert(!audio_pcm_to_stereo_s16(source, sizeof(source), 3U, destination,
                                    sizeof(destination), &written));
    assert(audio_pcm_stereo_bytes(4U, 0U) == 0U);
    assert(audio_pcm_stereo_bytes(4U, 6U) == 0U);
}

static void test_null_arguments_are_refused(void)
{
    const uint8_t source[] = {0x11, 0x22};
    uint8_t destination[8];
    size_t written = 0U;

    assert(!audio_pcm_to_stereo_s16(NULL, sizeof(source), 1U, destination,
                                    sizeof(destination), &written));
    assert(!audio_pcm_to_stereo_s16(source, sizeof(source), 1U, NULL,
                                    sizeof(destination), &written));
    assert(!audio_pcm_to_stereo_s16(source, sizeof(source), 1U, destination,
                                    sizeof(destination), NULL));
}

static void test_in_place_expansion_matches_the_copying_one(void)
{
    /* The decoder writes into the caller's PCM buffer, so mono has to grow
     * where it already sits. Reading backwards is what makes that safe, and
     * the result has to be indistinguishable from converting between two
     * buffers - so it is checked against exactly that. */
    static uint8_t buffer[1024U * 4U];
    static uint8_t reference[1024U * 4U];
    const size_t mono_bytes = 1024U * 2U;
    for (size_t index = 0U; index < mono_bytes; ++index) {
        buffer[index] = (uint8_t)(index * 7U + 3U);
    }
    memset(buffer + mono_bytes, GUARD, sizeof(buffer) - mono_bytes);

    size_t expected_written = 0U;
    assert(audio_pcm_to_stereo_s16(buffer, mono_bytes, 1U, reference, sizeof(reference),
                                   &expected_written));

    size_t written = 0U;
    assert(audio_pcm_mono_to_stereo_inplace_s16(buffer, mono_bytes, sizeof(buffer),
                                                &written));
    assert(written == expected_written);
    assert(written == mono_bytes * 2U);
    assert(memcmp(buffer, reference, written) == 0);
}

static void test_in_place_expansion_checks_its_room(void)
{
    /* Half the point of the bounds check is that this one writes into the
     * buffer it is reading: overrunning would corrupt whatever follows and
     * still leave the block half converted. */
    uint8_t buffer[16];
    fill_guarded(buffer, sizeof(buffer));
    const uint8_t source[] = {0x11, 0x22, 0x33, 0x44};
    memcpy(buffer, source, sizeof(source));
    size_t written = 12345U;

    assert(!audio_pcm_mono_to_stereo_inplace_s16(buffer, sizeof(source), 7U, &written));
    assert(memcmp(buffer, source, sizeof(source)) == 0);
    assert_guard_intact(buffer, sizeof(source), sizeof(buffer));
    assert(written == 12345U);

    assert(!audio_pcm_mono_to_stereo_inplace_s16(buffer, 3U, sizeof(buffer), &written));
    assert(!audio_pcm_mono_to_stereo_inplace_s16(NULL, 4U, sizeof(buffer), &written));
    assert(!audio_pcm_mono_to_stereo_inplace_s16(buffer, 4U, sizeof(buffer), NULL));

    /* Exactly twice the input is enough. */
    assert(audio_pcm_mono_to_stereo_inplace_s16(buffer, sizeof(source), 8U, &written));
    assert(written == 8U);
}

int main(void)
{
    test_mono_is_duplicated_into_both_channels();
    test_stereo_passes_through_unchanged();
    test_a_full_mp3_frame_fits_the_stereo_buffer();
    test_a_destination_one_byte_short_is_refused_untouched();
    test_a_partial_frame_is_refused();
    test_an_empty_block_is_not_an_error();
    test_an_unsupported_channel_count_is_refused();
    test_null_arguments_are_refused();
    test_in_place_expansion_matches_the_copying_one();
    test_in_place_expansion_checks_its_room();
    puts("audio_pcm_convert tests passed");
    return 0;
}
