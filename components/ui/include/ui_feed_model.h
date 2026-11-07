#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "audio_source.h"
#include "ui_menu.h"

typedef enum {
    UI_FEED_INTERNET_RADIO = 0,
    UI_FEED_USB,
    UI_FEED_SD_CARD,
    UI_FEED_BLUETOOTH,
    UI_FEED_FM,
    UI_FEED_DLNA,
    UI_FEED_YANDEX,
    UI_FEED_SETTINGS,
    UI_FEED_ITEM_COUNT,
} ui_feed_item_t;

typedef struct {
    uint8_t selected;
    ui_menu_visible_mask_t visible;
} ui_feed_model_t;

void ui_feed_model_init(ui_feed_model_t *model, uint8_t initial_index);

/* The carousel carries the same items as the list home screen, so it hides the
 * same ones. Which items those are, and the arithmetic of stepping past them,
 * live in ui_menu.c - this only holds the mask and asks. */
void ui_feed_model_set_source_visible(ui_feed_model_t *model, ui_menu_item_t item,
                                      bool visible);
ui_menu_visible_mask_t ui_feed_model_visible_mask(const ui_feed_model_t *model);
void ui_feed_model_move(ui_feed_model_t *model, int direction);
ui_feed_item_t ui_feed_model_selected(const ui_feed_model_t *model);

/* The cursor's step for one detent. The ring and the dots are both drawn from
 * the cursor, so whichever way this goes they go together; `reversed` is the
 * home screen's own setting and nothing else reads it. */
int ui_feed_model_turn_step(bool clockwise, bool reversed);

/* What the carousel draws: the item in `slot` of `slots` (counted from the
 * left, the selected one in the middle), and which dot is lit (counted from
 * the left over the items shown). Here rather than in ui.c so the claim that
 * the two move the same way is something a test can check on every panel's
 * slot count. */
ui_feed_item_t ui_feed_model_item_in_slot(const ui_feed_model_t *model, unsigned slot,
                                          unsigned slots);
unsigned ui_feed_model_lit_dot(const ui_feed_model_t *model);
bool ui_feed_model_activate(ui_feed_item_t item, audio_source_t *source_out);

/* Puts the cursor on the item that starts `source` - the inverse of
 * ui_feed_model_activate(). False, and the cursor left alone, when nothing on
 * the feed starts it.
 *
 * This is what makes leaving the player land on the icon it was started from.
 * The cursor cannot simply be remembered from the press that started it: a
 * source also starts from autoplay at boot and from the web UI, and neither
 * goes anywhere near this screen. */
bool ui_feed_model_select_source(ui_feed_model_t *model, audio_source_t source);
