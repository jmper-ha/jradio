#include "ui_radio_text.h"

#include <stdio.h>

void ui_radio_stream_text(char *text, size_t text_size, const char *codec, uint16_t bitrate_kbps)
{
    const char *display_codec = codec != NULL && codec[0] != '\0' ? codec : "--";

    if (bitrate_kbps > 0U) {
        snprintf(text, text_size, "%s  |  %u kbps", display_codec, (unsigned int)bitrate_kbps);
    } else {
        snprintf(text, text_size, "%s  |  -- kbps", display_codec);
    }
}
