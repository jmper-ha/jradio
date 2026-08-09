#include <assert.h>
#include <stdbool.h>

#include "radio_decoder.h"

static void test_aac_backend_is_available(void)
{
    assert(radio_decoder_is_supported(RADIO_STREAM_FORMAT_AAC));
    radio_decoder_t *aac = radio_decoder_create(RADIO_STREAM_FORMAT_AAC);
    assert(aac != NULL);
    radio_decoder_destroy(aac);
}

static void test_mp3_and_flac_backends_are_available(void)
{
    assert(radio_decoder_is_supported(RADIO_STREAM_FORMAT_MP3));
    radio_decoder_t *mp3 = radio_decoder_create(RADIO_STREAM_FORMAT_MP3);
    assert(mp3 != NULL);
    radio_decoder_destroy(mp3);
    assert(radio_decoder_is_supported(RADIO_STREAM_FORMAT_FLAC));
    assert(radio_decoder_is_supported(RADIO_STREAM_FORMAT_OGG_FLAC));
    radio_decoder_t *ogg_flac = radio_decoder_create(RADIO_STREAM_FORMAT_OGG_FLAC);
    assert(ogg_flac != NULL);
    radio_decoder_destroy(ogg_flac);
}

int main(void)
{
    test_aac_backend_is_available();
    test_mp3_and_flac_backends_are_available();
    return 0;
}
