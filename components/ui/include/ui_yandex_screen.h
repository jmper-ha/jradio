#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "yandex_auth.h"
#include "yandex_catalog.h"

/* What the Yandex Music screen shows, derived from the auth status alone.
 * Pure so the wording and the button hints are testable without a display -
 * the same split ui_player_state and ui_station_list use. */

#define UI_YANDEX_COUNTDOWN_MAX 31

typedef enum {
    /* No account yet: the status line plus, once the server has answered, the
     * code panel. */
    UI_YANDEX_MODE_PAIRING = 0,
    /* Linked, but there is nothing to list yet - loading, or it failed. */
    UI_YANDEX_MODE_MESSAGE,
    UI_YANDEX_MODE_LIST,
} ui_yandex_mode_t;

typedef struct {
    ui_yandex_mode_t mode;
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

/* `catalog_state` and `station_count` are ignored until an account is linked;
 * before that there is nothing to list and the screen is about the code. */
void ui_yandex_view_build(const yandex_auth_status_t *status,
                          yandex_catalog_state_t catalog_state, size_t station_count,
                          ui_yandex_view_t *view);

/* True while an attempt is in progress, which is what makes the screen poll
 * the countdown every tick instead of only on input. */
bool ui_yandex_view_is_busy(const yandex_auth_status_t *status);
