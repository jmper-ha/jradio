#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "radio_stream_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct radio_decoder radio_decoder_t;

typedef enum {
    RADIO_DECODER_PCM_READY = 0,
    RADIO_DECODER_NEED_MORE_DATA = 1,
    RADIO_DECODER_HEADER_READY = 2,
    RADIO_DECODER_END_OF_STREAM = 3,
    RADIO_DECODER_UNSUPPORTED = -1,
    RADIO_DECODER_ERROR = -2,
} radio_decoder_result_t;

typedef struct {
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t bits_per_sample;
    uint32_t bitrate_kbps;
    /* Most 16-bit stereo PCM one decoded frame can produce, which the caller's
     * output buffer has to hold in one piece - a frame is delivered whole or
     * not at all.
     *
     * Filled for FLAC, where it is not a constant: the block size is chosen by
     * the encoder, and the two values in ordinary use, 4096 and 4608 samples,
     * differ by exactly enough that a buffer sized for the first rejects every
     * frame of the second. Zero for the formats whose frames are small and
     * fixed, meaning the caller's usual buffer will do. */
    uint32_t pcm_frame_bytes;
} radio_decoder_info_t;

radio_decoder_t *radio_decoder_create(radio_stream_format_t format);
void radio_decoder_destroy(radio_decoder_t *decoder);
void radio_decoder_reset(radio_decoder_t *decoder);
radio_decoder_result_t radio_decoder_decode(radio_decoder_t *decoder, const uint8_t *input,
                                            size_t input_length, uint8_t *pcm_output,
                                            size_t pcm_capacity, size_t *bytes_consumed,
                                            size_t *pcm_bytes, radio_decoder_info_t *info);
bool radio_decoder_is_supported(radio_stream_format_t format);

#ifdef __cplusplus
}
#endif
