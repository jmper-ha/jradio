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

const char *ui_radio_station_name(bool name_from_list, const char *list_name,
                                  const char *stream_name)
{
    if (list_name == NULL) list_name = "";
    if (stream_name == NULL) stream_name = "";
    if (name_from_list || stream_name[0] == '\0') return list_name;
    return stream_name;
}

bool ui_radio_split_title(const char *icy, char *artist, size_t artist_size, char *title,
                          size_t title_size)
{
    if (artist == NULL || title == NULL || artist_size == 0U || title_size == 0U) {
        return false;
    }
    artist[0] = '\0';
    title[0] = '\0';
    if (icy == NULL || icy[0] == '\0') return false;

    const char *separator = strstr(icy, " - ");
    if (separator == NULL || separator == icy || separator[3] == '\0') {
        /* No separator, or nothing on one side of it: the whole string is the
         * best title available. */
        snprintf(title, title_size, "%s", icy);
        return false;
    }

    const size_t artist_length = (size_t)(separator - icy);
    if (artist_length >= artist_size) {
        /* The performer alone does not fit, so a split would only truncate it
         * into something misleading. */
        snprintf(title, title_size, "%s", icy);
        return false;
    }
    memcpy(artist, icy, artist_length);
    artist[artist_length] = '\0';
    snprintf(title, title_size, "%s", separator + 3);
    return true;
}
