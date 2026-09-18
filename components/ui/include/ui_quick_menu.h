#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "board_input.h"
#include "device_text.h"

/* The quick panel: the two or three settings somebody reaches for while the
 * music is playing, on the button that used to be called F2.
 *
 * Why a model of its own rather than a branch inside ui.c's input handler:
 * everything here is a state machine over presses - which row the knob owns,
 * whether the knob is moving the cursor or the value, when an untouched panel
 * closes itself - and that is exactly what a host test can check. ui.c is left
 * with two jobs the host cannot do: draw the window and apply the value.
 *
 * The values themselves deliberately do not live here. The sleep timer is a
 * deadline held by a service, the other three are device settings written to
 * the card, and pulling either into this file would drag ESP-IDF in with it.
 * The model answers "the knob moved one step up on this row"; the caller knows
 * what that means. */

typedef enum {
    UI_QUICK_ITEM_SLEEP = 0,
    UI_QUICK_ITEM_ALARM,
    UI_QUICK_ITEM_BT_OUTPUT,
    UI_QUICK_ITEM_BRIGHTNESS,
    UI_QUICK_ITEM_COUNT,
} ui_quick_item_t;

/* Which rows are on the panel, one bit per item. The Bluetooth row is the
 * only one that comes and goes while the firmware runs - it is there when the
 * module answers and the player is not listening *to* a phone - but the mask
 * is general because a build without the module has no row at all, and the
 * two reasons must not be told apart at the call site. */
typedef uint32_t ui_quick_mask_t;
#define UI_QUICK_VISIBLE(item) ((ui_quick_mask_t)1U << (unsigned)(item))
#define UI_QUICK_VISIBLE_ALL ((ui_quick_mask_t)~(ui_quick_mask_t)0U)

typedef struct {
    bool open;
    /* False: the knob moves between rows. True: the knob moves the value on
     * the row it is on. The encoder's click swaps the two, which is the whole
     * of the interaction. */
    bool editing;
    ui_quick_item_t item;
    ui_quick_mask_t visible;
    uint32_t last_input_ms;
} ui_quick_menu_t;

typedef enum {
    /* Nothing happened and nothing needs redrawing. */
    UI_QUICK_RESULT_NONE = 0,
    UI_QUICK_RESULT_OPENED,
    UI_QUICK_RESULT_CLOSED,
    /* The cursor moved, or the row changed hands between cursor and value.
     * Either way the window says something different now. */
    UI_QUICK_RESULT_MOVED,
    UI_QUICK_RESULT_STEP_UP,
    UI_QUICK_RESULT_STEP_DOWN,
} ui_quick_result_t;

/* How long an untouched panel stays up.
 *
 * Not decoration: while the window is open the knob belongs to it, so the
 * volume is out of reach. A panel left open by accident is a volume control
 * that stopped working, and ten seconds is the difference between a setting
 * somebody is reading and one they have walked away from. */
#define UI_QUICK_MENU_IDLE_MS 10000U

void ui_quick_menu_init(ui_quick_menu_t *state);

/* Setting a row invisible moves the cursor off it, and leaves edit mode with
 * it: a knob still assigned to a row that is no longer drawn would change a
 * value nobody can see. */
void ui_quick_menu_set_visible(ui_quick_menu_t *state, ui_quick_item_t item, bool visible);
bool ui_quick_menu_item_visible(const ui_quick_menu_t *state, ui_quick_item_t item);
uint8_t ui_quick_menu_visible_count(const ui_quick_menu_t *state);

bool ui_quick_menu_open(ui_quick_menu_t *state, uint32_t now_ms);
void ui_quick_menu_close(ui_quick_menu_t *state);

/* The whole interaction:
 *
 *   quick menu button  - opens, and closes again
 *   knob turned        - the next row, or the value one step
 *   knob pressed       - takes the row, and gives it back
 *   knob held          - closes, because holding means "back" everywhere else
 *   any other button   - closes, and the press is spent on closing
 *
 * Returns what the caller has to act on. A press that reaches this function
 * while the panel is closed only ever opens it, so ui.c can call this first
 * and carry on with the rest of its handler when the answer is NONE. */
ui_quick_result_t ui_quick_menu_handle(ui_quick_menu_t *state,
                                       board_input_action_t action,
                                       uint32_t now_ms);

bool ui_quick_menu_idle_expired(const ui_quick_menu_t *state, uint32_t now_ms);

/* The lengths the sleep row cycles through, the same list the web page offers:
 * a quarter of an hour apart up to an hour, then the two somebody puts a whole
 * record on for. Off is one of them, so the row is a ring rather than a range
 * with an edge to fall off. */
#define UI_QUICK_SLEEP_CHOICE_COUNT 7U
extern const uint16_t ui_quick_sleep_choices[UI_QUICK_SLEEP_CHOICE_COUNT];

/* The next length in `direction`, wrapping at both ends. A value that is not
 * on the list - a timer armed from the web with something else - is answered
 * with the nearest step in that direction rather than snapped silently. */
uint16_t ui_quick_sleep_step(uint16_t minutes, int direction);

const char *ui_quick_item_label(ui_quick_item_t item, device_language_t language);
