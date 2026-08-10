#include "ui_radio_text.h"

#include <stdio.h>

#include "radio_stream_format.h"

void ui_radio_stream_text(char *text, size_t text_size, const char *codec,
                          uint16_t bitrate_kbps, uint32_t sample_rate_hz)
{
    const char *display_codec = codec != NULL && codec[0] != '\0' ? codec : "--";
    char bitrate_text[16];
    char rate_text[16];

    if (bitrate_kbps > 0U) {
        snprintf(bitrate_text, sizeof(bitrate_text), "%u kbps", (unsigned int)bitrate_kbps);
    } else {
        snprintf(bitrate_text, sizeof(bitrate_text), "-- kbps");
    }

    if (sample_rate_hz > 0U) {
        snprintf(rate_text, sizeof(rate_text), "%u", (unsigned int)sample_rate_hz);
    } else {
        snprintf(rate_text, sizeof(rate_text), "--");
    }

    snprintf(text, text_size, "%s  |  %s  |  %s", display_codec, bitrate_text, rate_text);
}

void ui_radio_stream_text_for_url(char *text, size_t text_size, const char *url)
{
    const radio_stream_format_t format = radio_stream_format_from_url(url);
    ui_radio_stream_text(text, text_size, radio_stream_format_codec_name(format), 0U, 0U);
}
