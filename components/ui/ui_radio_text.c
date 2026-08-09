#include "ui_radio_text.h"

#include <stdio.h>

#include "radio_stream_format.h"

void ui_radio_stream_text(char *text, size_t text_size, const char *codec, uint16_t bitrate_kbps)
{
    const char *display_codec = codec != NULL && codec[0] != '\0' ? codec : "--";

    if (bitrate_kbps > 0U) {
        snprintf(text, text_size, "%s  |  %u kbps", display_codec, (unsigned int)bitrate_kbps);
    } else {
        snprintf(text, text_size, "%s  |  -- kbps", display_codec);
    }
}

void ui_radio_stream_text_for_url(char *text, size_t text_size, const char *url)
{
    const radio_stream_format_t format = radio_stream_format_from_url(url);
    ui_radio_stream_text(text, text_size, radio_stream_format_codec_name(format), 0U);
}
