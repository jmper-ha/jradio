#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ICY_METADATA_TITLE_MAX_LEN 192
#define ICY_METADATA_BLOCK_MAX_LEN (255U * 16U)

typedef enum {
    ICY_METADATA_OK = 0,
    ICY_METADATA_INVALID_ARG,
    ICY_METADATA_OUTPUT_TOO_SMALL,
} icy_metadata_result_t;

typedef void (*icy_metadata_title_callback_t)(void *context, const char *title);

typedef struct {
    size_t interval;
    size_t audio_remaining;
    size_t metadata_remaining;
    size_t metadata_length;
    icy_metadata_title_callback_t title_callback;
    void *title_context;
    char metadata[ICY_METADATA_BLOCK_MAX_LEN + 1U];
} icy_metadata_t;

void icy_metadata_init(icy_metadata_t *parser, size_t interval,
                       icy_metadata_title_callback_t title_callback, void *title_context);

icy_metadata_result_t icy_metadata_feed(icy_metadata_t *parser, const uint8_t *input,
                                        size_t input_length, uint8_t *audio_output,
                                        size_t audio_capacity, size_t *audio_length);

#ifdef __cplusplus
}
#endif
