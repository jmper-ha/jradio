#include "ui_about.h"

#include <stdio.h>
#include <string.h>

/* A reading the device could not take. Not an empty string: a blank where a
 * version belongs reads as the screen having failed rather than as the device
 * honestly not knowing, and those are different things to a person looking
 * for why the web page is stale. */
static const char *unknown_text(bool english)
{
    return english ? "unknown" : "неизвестно";
}

static void line(char *out, const char *label, const char *value, bool english)
{
    snprintf(out, UI_ABOUT_LINE_MAX + 1U, "%s: %s",
             label, value[0] != '\0' ? value : unknown_text(english));
}

void ui_about_build(const version_info_t *info, bool english, ui_about_lines_t *lines)
{
    if (lines == NULL) return;
    memset(lines, 0, sizeof(*lines));
    version_info_t empty;
    if (info == NULL) {
        memset(&empty, 0, sizeof(empty));
        info = &empty;
    }

    line(lines->firmware, english ? "Firmware" : "Прошивка", info->firmware.version, english);
    line(lines->built, english ? "Built" : "Собрана", info->firmware.built, english);
    /* The web assets are a version of their own because they are flashed by
     * their own command. Saying so on the same screen is the whole point of
     * the screen. */
    line(lines->web, english ? "Web UI" : "Веб-интерфейс", info->web.version, english);
    line(lines->idf, "ESP-IDF", info->idf, english);

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
                 english ? "Web UI is from another build"
                         : "Веб-интерфейс от другой сборки");
    }
}
