#include "ui_radio_text.h"

#include <string.h>

#include <stdio.h>

#include "radio_stream_format.h"

/* The three fields and what joins them. Both forms carry exactly the same
 * readings in the same order - only the separator differs - so a reader who
 * has seen one panel recognises the other. */
static void ui_radio_stream_fields(char *text, size_t text_size, const char *codec,
                                   uint16_t bitrate_kbps, uint32_t sample_rate_hz,
                                   const char *separator)
{
    const char *display_codec = codec != NULL && codec[0] != '\0' ? codec : "--";
    char rate_text[16];

    if (sample_rate_hz > 0U) {
        snprintf(rate_text, sizeof(rate_text), "%u", (unsigned int)sample_rate_hz);
    } else {
        snprintf(rate_text, sizeof(rate_text), "--");
    }

    /* A stream that plays with no bitrate to report - a phone over
     * Bluetooth, a server that sends no icy-br - shows the two readings it
     * has rather than a dash where the third would be. The dash stays only
     * while nothing plays yet, when it says "not known yet" rather than
     * "not going to be", which is the reading the web page makes too. */
    if (bitrate_kbps == 0U && sample_rate_hz > 0U) {
        snprintf(text, text_size, "%s%s%s", display_codec, separator, rate_text);
        return;
    }

    char bitrate_text[16];
    if (bitrate_kbps > 0U) {
        snprintf(bitrate_text, sizeof(bitrate_text), "%u kbps", (unsigned int)bitrate_kbps);
    } else {
        snprintf(bitrate_text, sizeof(bitrate_text), "-- kbps");
    }
    snprintf(text, text_size, "%s%s%s%s%s", display_codec, separator, bitrate_text, separator,
             rate_text);
}

void ui_radio_stream_text(char *text, size_t text_size, const char *codec,
                          uint16_t bitrate_kbps, uint32_t sample_rate_hz)
{
    ui_radio_stream_fields(text, text_size, codec, bitrate_kbps, sample_rate_hz, "  |  ");
}

void ui_radio_stream_lines(char *text, size_t text_size, const char *codec,
                           uint16_t bitrate_kbps, uint32_t sample_rate_hz)
{
    ui_radio_stream_fields(text, text_size, codec, bitrate_kbps, sample_rate_hz, "\n");
}

void ui_radio_stream_text_for_url(char *text, size_t text_size, const char *url)
{
    const radio_stream_format_t format = radio_stream_format_from_url(url);
    ui_radio_stream_text(text, text_size, radio_stream_format_codec_name(format), 0U, 0U);
}
