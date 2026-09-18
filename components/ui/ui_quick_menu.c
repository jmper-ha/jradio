#include "ui_quick_menu.h"

const uint16_t ui_quick_sleep_choices[UI_QUICK_SLEEP_CHOICE_COUNT] = {
    0U, 15U, 30U, 45U, 60U, 90U, 120U,
};

static bool item_visible(ui_quick_mask_t visible, ui_quick_item_t item)
{
    if ((unsigned)item >= (unsigned)UI_QUICK_ITEM_COUNT) return false;
    return (visible & UI_QUICK_VISIBLE(item)) != 0U;
}

/* The next visible row in `direction`, wrapping. Walks at most one full turn,
 * so a mask with a single row answers with that row rather than spinning. */
static ui_quick_item_t item_step(ui_quick_item_t item, int direction, ui_quick_mask_t visible)
{
    const int count = (int)UI_QUICK_ITEM_COUNT;
    int next = (int)item;
    for (int taken = 0; taken < count; ++taken) {
        next += direction > 0 ? 1 : -1;
        if (next >= count) next = 0;
        if (next < 0) next = count - 1;
        if (item_visible(visible, (ui_quick_item_t)next)) break;
    }
    return (ui_quick_item_t)next;
}

static ui_quick_item_t first_visible(ui_quick_mask_t visible)
{
    for (unsigned index = 0U; index < (unsigned)UI_QUICK_ITEM_COUNT; ++index) {
        if (item_visible(visible, (ui_quick_item_t)index)) return (ui_quick_item_t)index;
    }
    return UI_QUICK_ITEM_SLEEP;
}

void ui_quick_menu_init(ui_quick_menu_t *state)
{
    if (state == NULL) return;
    state->open = false;
    state->editing = false;
    state->item = UI_QUICK_ITEM_SLEEP;
    /* The sleep timer and the alarm are always there; the other two are told
     * to us. Starting with all four would draw a Bluetooth row for one pass on
     * a board that has no module. */
    state->visible = UI_QUICK_VISIBLE(UI_QUICK_ITEM_SLEEP) | UI_QUICK_VISIBLE(UI_QUICK_ITEM_ALARM);
    state->last_input_ms = 0U;
}

void ui_quick_menu_set_visible(ui_quick_menu_t *state, ui_quick_item_t item, bool visible)
{
    if (state == NULL || (unsigned)item >= (unsigned)UI_QUICK_ITEM_COUNT) return;
    if (visible) {
        state->visible |= UI_QUICK_VISIBLE(item);
        return;
    }
    state->visible &= ~UI_QUICK_VISIBLE(item);
    if (state->item != item) return;
    /* The cursor was standing on it. Leave edit mode with it: a knob assigned
     * to a row that is no longer drawn would move a value nobody can see. */
    state->editing = false;
    state->item = first_visible(state->visible);
}

bool ui_quick_menu_item_visible(const ui_quick_menu_t *state, ui_quick_item_t item)
{
    if (state == NULL) return false;
    return item_visible(state->visible, item);
}

uint8_t ui_quick_menu_visible_count(const ui_quick_menu_t *state)
{
    if (state == NULL) return 0U;
    uint8_t count = 0U;
    for (unsigned index = 0U; index < (unsigned)UI_QUICK_ITEM_COUNT; ++index) {
        if (item_visible(state->visible, (ui_quick_item_t)index)) ++count;
    }
    return count;
}

bool ui_quick_menu_open(ui_quick_menu_t *state, uint32_t now_ms)
{
    if (state == NULL || state->open) return false;
    if (ui_quick_menu_visible_count(state) == 0U) return false;
    state->open = true;
    /* Always on the cursor, never mid-edit: the panel is opened to look at it
     * first. And always on the first row - coming back to where the knob was
     * left ten minutes ago is a riddle, not a convenience. */
    state->editing = false;
    state->item = first_visible(state->visible);
    state->last_input_ms = now_ms;
    return true;
}

void ui_quick_menu_close(ui_quick_menu_t *state)
{
    if (state == NULL) return;
    state->open = false;
    state->editing = false;
}

ui_quick_result_t ui_quick_menu_handle(ui_quick_menu_t *state,
                                       board_input_action_t action,
                                       uint32_t now_ms)
{
    if (state == NULL) return UI_QUICK_RESULT_NONE;

    if (!state->open) {
        if (action != BOARD_INPUT_ACTION_QUICK_MENU) return UI_QUICK_RESULT_NONE;
        return ui_quick_menu_open(state, now_ms) ? UI_QUICK_RESULT_OPENED
                                                 : UI_QUICK_RESULT_NONE;
    }

    state->last_input_ms = now_ms;

    switch (action) {
    case BOARD_INPUT_ACTION_QUICK_MENU:
    case BOARD_INPUT_ACTION_ENCODER_LONG:
    /* The transport keys close the panel and go no further. The press is
     * spent on closing: acting on it as well would mean a blind change - the
     * window is over the screen that would have shown what it did. */
    case BOARD_INPUT_ACTION_BTN_PREV:
    case BOARD_INPUT_ACTION_BTN_NEXT:
    case BOARD_INPUT_ACTION_SLEEP_BUTTON:
        ui_quick_menu_close(state);
        return UI_QUICK_RESULT_CLOSED;
    case BOARD_INPUT_ACTION_ENCODER_BUTTON:
        state->editing = !state->editing;
        return UI_QUICK_RESULT_MOVED;
    case BOARD_INPUT_ACTION_ENCODER_LEFT:
    case BOARD_INPUT_ACTION_ENCODER_RIGHT: {
        const int direction = action == BOARD_INPUT_ACTION_ENCODER_RIGHT ? 1 : -1;
        if (state->editing) {
            return direction > 0 ? UI_QUICK_RESULT_STEP_UP : UI_QUICK_RESULT_STEP_DOWN;
        }
        const ui_quick_item_t next = item_step(state->item, direction, state->visible);
        if (next == state->item) return UI_QUICK_RESULT_NONE;
        state->item = next;
        return UI_QUICK_RESULT_MOVED;
    }
    default:
        break;
    }
    return UI_QUICK_RESULT_NONE;
}

bool ui_quick_menu_idle_expired(const ui_quick_menu_t *state, uint32_t now_ms)
{
    if (state == NULL || !state->open) return false;
    return (uint32_t)(now_ms - state->last_input_ms) >= UI_QUICK_MENU_IDLE_MS;
}

uint16_t ui_quick_sleep_step(uint16_t minutes, int direction)
{
    const unsigned int last = UI_QUICK_SLEEP_CHOICE_COUNT - 1U;
    if (direction >= 0) {
        for (unsigned int index = 0U; index < UI_QUICK_SLEEP_CHOICE_COUNT; ++index) {
            if (ui_quick_sleep_choices[index] > minutes) return ui_quick_sleep_choices[index];
        }
        return ui_quick_sleep_choices[0];
    }
    for (unsigned int step = 0U; step < UI_QUICK_SLEEP_CHOICE_COUNT; ++step) {
        const unsigned int index = last - step;
        if (ui_quick_sleep_choices[index] < minutes) return ui_quick_sleep_choices[index];
    }
    return ui_quick_sleep_choices[last];
}

const char *ui_quick_item_label(ui_quick_item_t item, device_language_t language)
{
    switch (item) {
    case UI_QUICK_ITEM_SLEEP: return device_text(DEVICE_TEXT_ROW_SLEEP_TIMER, language);
    case UI_QUICK_ITEM_ALARM: return device_text(DEVICE_TEXT_ROW_ALARM, language);
    case UI_QUICK_ITEM_BT_OUTPUT: return device_text(DEVICE_TEXT_ROW_BT_OUTPUT, language);
    case UI_QUICK_ITEM_BRIGHTNESS: return device_text(DEVICE_TEXT_ROW_BRIGHTNESS, language);
    default: break;
    }
    return "";
}
