#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ui_level_text.h"

static const char *show(uint8_t phase, float gain, float wanted, bool capped)
{
    static char out[24];
    const audio_level_status_t status = {
        .phase = phase, .gain_db = gain, .wanted_db = wanted, .capped = capped};
    if (!ui_level_text(&status, "дБ", "…", "макс", out, sizeof(out))) return NULL;
    return out;
}

int main(void)
{
    assert(show(AUDIO_LEVEL_OFF, -4.0f, -4.0f, false) == NULL);
    assert(strcmp(show(AUDIO_LEVEL_MEASURING, 0.0f, 0.0f, false), "…") == 0);
    assert(strcmp(show(AUDIO_LEVEL_LEARNING, -3.6f, -6.0f, false), "\xE2\x80\x93" "4 дБ") == 0);
    assert(strcmp(show(AUDIO_LEVEL_SETTLED, 2.4f, 2.4f, false), "+2 дБ") == 0);
    assert(strcmp(show(AUDIO_LEVEL_SETTLED, 0.3f, 0.3f, false), "0 дБ") == 0);
    // A lift the volume had no room for says so; a cut never is one.
    assert(strcmp(show(AUDIO_LEVEL_SETTLED, 5.0f, 5.0f, true), "макс") == 0);
    assert(strcmp(show(AUDIO_LEVEL_SETTLED, -5.0f, -5.0f, true), "\xE2\x80\x93" "5 дБ") == 0);
    // The nudge is in the figure; with the levelling off it is the figure.
    static char out[24];
    audio_level_status_t nudged = {.phase = AUDIO_LEVEL_SETTLED, .gain_db = -6.2f,
                                   .wanted_db = -6.2f, .nudge_db = 2};
    assert(ui_level_text(&nudged, "дБ", "…", "макс", out, sizeof(out)));
    assert(strcmp(out, "\xE2\x80\x93" "4 дБ") == 0);
    nudged = (audio_level_status_t){.phase = AUDIO_LEVEL_OFF, .nudge_db = -3};
    assert(ui_level_text(&nudged, "дБ", "…", "макс", out, sizeof(out)));
    assert(strcmp(out, "\xE2\x80\x93" "3 дБ") == 0);
    nudged = (audio_level_status_t){.phase = AUDIO_LEVEL_OFF, .nudge_db = 3, .capped = true};
    assert(ui_level_text(&nudged, "дБ", "…", "макс", out, sizeof(out)));
    assert(strcmp(out, "макс") == 0);
    // The colour: the station's while it settles, its own once it has.
    assert(ui_level_settling(&(audio_level_status_t){.phase = AUDIO_LEVEL_MEASURING}));
    assert(ui_level_settling(&(audio_level_status_t){.phase = AUDIO_LEVEL_LEARNING}));
    assert(!ui_level_settling(&(audio_level_status_t){.phase = AUDIO_LEVEL_SETTLED}));
    assert(!ui_level_settling(&(audio_level_status_t){.phase = AUDIO_LEVEL_OFF, .nudge_db = 2}));
    assert(!ui_level_settling(NULL));
    // A buffer too small is cut, not overrun.
    char tiny[4];
    const audio_level_status_t status = {.phase = AUDIO_LEVEL_SETTLED, .gain_db = -9.0f};
    assert(ui_level_text(&status, "дБ", "…", "макс", tiny, sizeof(tiny)));
    assert(strlen(tiny) < sizeof(tiny));
    assert(!ui_level_text(NULL, "дБ", "…", "макс", tiny, sizeof(tiny)) && tiny[0] == '\0');
    puts("ui level text tests passed");
    return 0;
}
