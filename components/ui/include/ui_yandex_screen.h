#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "yandex_auth.h"

/* What the Yandex Music screen shows, derived from the auth status alone.
 * Pure so the wording and the button hints are testable without a display -
 * the same split ui_player_state and ui_station_list use. */

#define UI_YANDEX_COUNTDOWN_MAX 31

typedef struct {
    const char *status;
    /* Empty unless a code is live. The screen hides the whole block then,
     * rather than showing an empty box where a code used to be. */
    const char *code;
    const char *url;
    char countdown[UI_YANDEX_COUNTDOWN_MAX + 1];
    /* Which buttons do something right now. The four function keys mean
     * different things on every screen, so the screen has to say. */
    const char *hint;
    bool show_code;
} ui_yandex_view_t;

void ui_yandex_view_build(const yandex_auth_status_t *status, ui_yandex_view_t *view);

/* True while an attempt is in progress, which is what makes the screen poll
 * the countdown every tick instead of only on input. */
bool ui_yandex_view_is_busy(const yandex_auth_status_t *status);
