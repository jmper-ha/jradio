#include "icy_metadata.h"

#include <stdbool.h>
#include <string.h>

static void icy_metadata_publish_title(icy_metadata_t *parser)
{
    static const char prefix[] = "StreamTitle='";
    const char *value;
    const char *end;
    size_t length;
    char title[ICY_METADATA_TITLE_MAX_LEN];

    parser->metadata[parser->metadata_length] = '\0';
    value = strstr(parser->metadata, prefix);
    if (value == NULL) {
        return;
    }
    value += sizeof(prefix) - 1U;
    end = strchr(value, '\'');
    if (end == NULL) {
        return;
    }

    length = (size_t)(end - value);
    if (length >= sizeof(title)) {
        length = sizeof(title) - 1U;
    }
    memcpy(title, value, length);
    title[length] = '\0';
    if (parser->title_callback != NULL) {
        parser->title_callback(parser->title_context, title);
    }
}

void icy_metadata_init(icy_metadata_t *parser, size_t interval,
                       icy_metadata_title_callback_t title_callback, void *title_context)
{
    if (parser == NULL) {
        return;
    }

    memset(parser, 0, sizeof(*parser));
    parser->interval = interval;
    parser->audio_remaining = interval;
    parser->title_callback = title_callback;
    parser->title_context = title_context;
}

icy_metadata_result_t icy_metadata_feed(icy_metadata_t *parser, const uint8_t *input,
                                        size_t input_length, uint8_t *audio_output,
                                        size_t audio_capacity, size_t *audio_length)
{
    size_t input_index;
    size_t output_index = 0;

    if (parser == NULL || input == NULL || audio_output == NULL || audio_length == NULL) {
        return ICY_METADATA_INVALID_ARG;
    }

    for (input_index = 0; input_index < input_length; ++input_index) {
        uint8_t byte = input[input_index];

        if (parser->interval == 0U) {
            if (output_index == audio_capacity) {
                return ICY_METADATA_OUTPUT_TOO_SMALL;
            }
            audio_output[output_index++] = byte;
            continue;
        }

        if (parser->audio_remaining > 0U) {
            if (output_index == audio_capacity) {
                return ICY_METADATA_OUTPUT_TOO_SMALL;
            }
            audio_output[output_index++] = byte;
            parser->audio_remaining--;
            continue;
        }

        if (parser->metadata_remaining == 0U) {
            parser->metadata_remaining = (size_t)byte * 16U;
            parser->metadata_length = 0U;
            if (parser->metadata_remaining == 0U) {
                parser->audio_remaining = parser->interval;
            }
            continue;
        }

        if (parser->metadata_length < ICY_METADATA_BLOCK_MAX_LEN) {
            parser->metadata[parser->metadata_length++] = (char)byte;
        }
        parser->metadata_remaining--;
        if (parser->metadata_remaining == 0U) {
            icy_metadata_publish_title(parser);
            parser->audio_remaining = parser->interval;
        }
    }

    *audio_length = output_index;
    return ICY_METADATA_OK;
}
