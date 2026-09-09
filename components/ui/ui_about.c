#include "ui_about.h"

#include <stdio.h>
#include <string.h>

/* A reading the device could not take is spelled out rather than left blank: a
 * gap where a version belongs reads as the screen having failed rather than as
 * the device honestly not knowing, and those are different things to somebody
 * looking for why the web page is stale. */
static void line(char *out, device_text_id_t label, const char *value,
                 device_language_t language)
{
    snprintf(out, UI_ABOUT_LINE_MAX + 1U, "%s: %s", device_text(label, language),
             value[0] != '\0' ? value : device_text(DEVICE_TEXT_ABOUT_UNKNOWN, language));
}

void ui_about_build(const version_info_t *info, device_language_t language,
                    ui_about_lines_t *lines)
{
    if (lines == NULL) return;
    memset(lines, 0, sizeof(*lines));
    version_info_t empty;
    if (info == NULL) {
        memset(&empty, 0, sizeof(empty));
        info = &empty;
    }

    line(lines->firmware, DEVICE_TEXT_ABOUT_FIRMWARE, info->firmware.version, language);
    line(lines->built, DEVICE_TEXT_ABOUT_BUILT, info->firmware.built, language);
    /* The web assets are a version of their own because they are flashed by
     * their own command. Saying so on the same screen is the whole point of
     * the screen. */
    line(lines->web, DEVICE_TEXT_ABOUT_WEB, info->web.version, language);
    line(lines->idf, DEVICE_TEXT_ABOUT_IDF, info->idf, language);

    /* The notice speaks only when the two halves disagree, and stays quiet
     * when the web stamp is simply missing: an image flashed before the stamp
     * existed is old, not mismatched, and the "unknown" on its own line has
     * already said so. Crying mismatch there would train the reader to ignore
     * the line that matters. */
    if (info->firmware.present && info->web.present &&
        strcmp(info->firmware.version, info->web.version) != 0) {
        /* Short on purpose, and measured rather than guessed: Cyrillic is two
         * bytes a letter, and the sentence this replaced needed 85 of them
         * against a line that holds 63 - the compiler caught it. It also has
         * to be readable on the narrowest panel, where 29 letters is about
         * what fits. What to do about it belongs in the documentation, not on
         * a 240 px screen. */
        snprintf(lines->notice, sizeof(lines->notice), "%s",
                 device_text(DEVICE_TEXT_ABOUT_MISMATCH, language));
    }
}
