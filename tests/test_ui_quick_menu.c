#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "ui_quick_menu.h"

/* The panel as ui.c has it once the Bluetooth module has answered: all four
 * rows. A build or a moment without the module is the other case, and it is
 * tested separately rather than assumed. */
static void open_with_all_rows(ui_quick_menu_t *state)
{
    ui_quick_menu_init(state);
    ui_quick_menu_set_visible(state, UI_QUICK_ITEM_BT_OUTPUT, true);
    ui_quick_menu_set_visible(state, UI_QUICK_ITEM_BRIGHTNESS, true);
    assert(ui_quick_menu_open(state, 1000U));
}

static void test_the_button_opens_and_closes(void)
{
    ui_quick_menu_t state;
    ui_quick_menu_init(&state);

    /* Closed, every other press is somebody else's. */
    assert(ui_quick_menu_handle(&state, BOARD_INPUT_ACTION_ENCODER_RIGHT, 100U) ==
           UI_QUICK_RESULT_NONE);
    assert(ui_quick_menu_handle(&state, BOARD_INPUT_ACTION_ENCODER_BUTTON, 100U) ==
           UI_QUICK_RESULT_NONE);
    assert(!state.open);

    assert(ui_quick_menu_handle(&state, BOARD_INPUT_ACTION_QUICK_MENU, 100U) ==
           UI_QUICK_RESULT_OPENED);
    assert(state.open);
    assert(!state.editing);
    assert(ui_quick_menu_handle(&state, BOARD_INPUT_ACTION_QUICK_MENU, 200U) ==
           UI_QUICK_RESULT_CLOSED);
    assert(!state.open);
}

static void test_the_knob_walks_the_rows_and_wraps(void)
{
    ui_quick_menu_t state;
    open_with_all_rows(&state);

    assert(state.item == UI_QUICK_ITEM_SLEEP);
    assert(ui_quick_menu_handle(&state, BOARD_INPUT_ACTION_ENCODER_RIGHT, 1100U) ==
           UI_QUICK_RESULT_MOVED);
    assert(state.item == UI_QUICK_ITEM_ALARM);
    assert(ui_quick_menu_handle(&state, BOARD_INPUT_ACTION_ENCODER_RIGHT, 1200U) ==
           UI_QUICK_RESULT_MOVED);
    assert(state.item == UI_QUICK_ITEM_BRIGHTNESS);
    assert(ui_quick_menu_handle(&state, BOARD_INPUT_ACTION_ENCODER_RIGHT, 1300U) ==
           UI_QUICK_RESULT_MOVED);
    assert(state.item == UI_QUICK_ITEM_BT_OUTPUT);
    /* Round the end, so the last row is not a dead stop. */
    assert(ui_quick_menu_handle(&state, BOARD_INPUT_ACTION_ENCODER_RIGHT, 1400U) ==
           UI_QUICK_RESULT_MOVED);
    assert(state.item == UI_QUICK_ITEM_SLEEP);
    assert(ui_quick_menu_handle(&state, BOARD_INPUT_ACTION_ENCODER_LEFT, 1500U) ==
           UI_QUICK_RESULT_MOVED);
    assert(state.item == UI_QUICK_ITEM_BT_OUTPUT);
}

static void test_the_click_hands_the_knob_to_the_value(void)
{
    ui_quick_menu_t state;
    open_with_all_rows(&state);

    /* Not editing: a turn moves between rows. */
    assert(ui_quick_menu_handle(&state, BOARD_INPUT_ACTION_ENCODER_RIGHT, 1100U) ==
           UI_QUICK_RESULT_MOVED);
    const ui_quick_item_t row = state.item;

    assert(ui_quick_menu_handle(&state, BOARD_INPUT_ACTION_ENCODER_BUTTON, 1200U) ==
           UI_QUICK_RESULT_MOVED);
    assert(state.editing);
    /* Editing: the same turn is a step, and the row does not move under it. */
    assert(ui_quick_menu_handle(&state, BOARD_INPUT_ACTION_ENCODER_RIGHT, 1300U) ==
           UI_QUICK_RESULT_STEP_UP);
    assert(state.item == row);
    assert(ui_quick_menu_handle(&state, BOARD_INPUT_ACTION_ENCODER_LEFT, 1400U) ==
           UI_QUICK_RESULT_STEP_DOWN);
    assert(state.item == row);

    assert(ui_quick_menu_handle(&state, BOARD_INPUT_ACTION_ENCODER_BUTTON, 1500U) ==
           UI_QUICK_RESULT_MOVED);
    assert(!state.editing);
    assert(ui_quick_menu_handle(&state, BOARD_INPUT_ACTION_ENCODER_RIGHT, 1600U) ==
           UI_QUICK_RESULT_MOVED);
    assert(state.item != row);
}

static void test_holding_and_the_other_buttons_close_it(void)
{
    const board_input_action_t closing[] = {
        BOARD_INPUT_ACTION_ENCODER_LONG,
        BOARD_INPUT_ACTION_BTN_PREV,
        BOARD_INPUT_ACTION_BTN_NEXT,
        BOARD_INPUT_ACTION_SLEEP_BUTTON,
        BOARD_INPUT_ACTION_QUICK_MENU,
    };
    for (size_t index = 0U; index < sizeof(closing) / sizeof(closing[0]); ++index) {
        ui_quick_menu_t state;
        open_with_all_rows(&state);
        /* From inside a value, too: closing has to give the knob back whatever
           it was doing with it. */
        assert(ui_quick_menu_handle(&state, BOARD_INPUT_ACTION_ENCODER_BUTTON, 1100U) ==
               UI_QUICK_RESULT_MOVED);
        assert(state.editing);
        assert(ui_quick_menu_handle(&state, closing[index], 1200U) == UI_QUICK_RESULT_CLOSED);
        assert(!state.open);
        assert(!state.editing);
    }
}

static void test_a_row_that_disappears_takes_the_cursor_with_it(void)
{
    ui_quick_menu_t state;
    open_with_all_rows(&state);

    /* Stand on the Bluetooth row, in edit mode, and have the module go away -
       which is exactly what a phone taking the module does. */
    while (state.item != UI_QUICK_ITEM_BT_OUTPUT) {
        (void)ui_quick_menu_handle(&state, BOARD_INPUT_ACTION_ENCODER_RIGHT, 1100U);
    }
    (void)ui_quick_menu_handle(&state, BOARD_INPUT_ACTION_ENCODER_BUTTON, 1200U);
    assert(state.editing);

    ui_quick_menu_set_visible(&state, UI_QUICK_ITEM_BT_OUTPUT, false);
    assert(state.item != UI_QUICK_ITEM_BT_OUTPUT);
    assert(!state.editing);
    assert(!ui_quick_menu_item_visible(&state, UI_QUICK_ITEM_BT_OUTPUT));
    assert(ui_quick_menu_visible_count(&state) == 3U);

    /* And the walk no longer stops on it. */
    for (int step = 0; step < 6; ++step) {
        (void)ui_quick_menu_handle(&state, BOARD_INPUT_ACTION_ENCODER_RIGHT, 1300U);
        assert(state.item != UI_QUICK_ITEM_BT_OUTPUT);
    }
}

static void test_one_row_left_is_not_a_spin(void)
{
    ui_quick_menu_t state;
    ui_quick_menu_init(&state);
    ui_quick_menu_set_visible(&state, UI_QUICK_ITEM_ALARM, false);
    assert(ui_quick_menu_visible_count(&state) == 1U);
    assert(ui_quick_menu_open(&state, 100U));
    assert(state.item == UI_QUICK_ITEM_SLEEP);
    /* Nothing to move to, so nothing moves - and nothing needs redrawing. */
    assert(ui_quick_menu_handle(&state, BOARD_INPUT_ACTION_ENCODER_RIGHT, 200U) ==
           UI_QUICK_RESULT_NONE);
    assert(state.item == UI_QUICK_ITEM_SLEEP);
}

static void test_a_panel_with_no_rows_does_not_open(void)
{
    ui_quick_menu_t state;
    ui_quick_menu_init(&state);
    ui_quick_menu_set_visible(&state, UI_QUICK_ITEM_SLEEP, false);
    ui_quick_menu_set_visible(&state, UI_QUICK_ITEM_ALARM, false);
    assert(ui_quick_menu_visible_count(&state) == 0U);
    assert(!ui_quick_menu_open(&state, 100U));
    assert(ui_quick_menu_handle(&state, BOARD_INPUT_ACTION_QUICK_MENU, 100U) ==
           UI_QUICK_RESULT_NONE);
    assert(!state.open);
}

static void test_an_untouched_panel_closes_itself(void)
{
    ui_quick_menu_t state;
    open_with_all_rows(&state);
    assert(!ui_quick_menu_idle_expired(&state, 1000U + UI_QUICK_MENU_IDLE_MS - 1U));
    assert(ui_quick_menu_idle_expired(&state, 1000U + UI_QUICK_MENU_IDLE_MS));

    /* Every press the panel takes puts the clock back, including one that
       changes nothing on screen. */
    (void)ui_quick_menu_handle(&state, BOARD_INPUT_ACTION_ENCODER_RIGHT,
                               1000U + UI_QUICK_MENU_IDLE_MS - 1U);
    assert(!ui_quick_menu_idle_expired(&state, 1000U + UI_QUICK_MENU_IDLE_MS));

    /* Wrap-safe: the tick is a 32-bit millisecond counter and the panel may be
       opened a moment before it turns over. */
    ui_quick_menu_close(&state);
    assert(ui_quick_menu_open(&state, 0xFFFFFF00U));
    assert(!ui_quick_menu_idle_expired(&state, 0xFFFFFF00U + 1000U));
    assert(ui_quick_menu_idle_expired(&state, 0xFFFFFF00U + UI_QUICK_MENU_IDLE_MS));

    /* A closed panel never expires, or the poll would close it once a pass. */
    ui_quick_menu_close(&state);
    assert(!ui_quick_menu_idle_expired(&state, 0xFFFFFFFFU));
}

/* Every function has a row of its own: the window is as tall as the list, and
   the point of the panel is that nothing is hidden behind a scroll. */
static void test_every_function_has_a_row(void)
{
    ui_quick_menu_t state;
    open_with_all_rows(&state);
    assert(ui_quick_menu_visible_count(&state) == 4U);
    assert(ui_quick_menu_visible_count(&state) <= (uint8_t)UI_QUICK_ROWS);

    /* The speaker's row is last: it is the one that can go away, and a row
       that vanished from the middle would move everything under it. */
    assert(ui_quick_menu_row_item(&state, 0U) == UI_QUICK_ITEM_SLEEP);
    assert(ui_quick_menu_row_item(&state, 1U) == UI_QUICK_ITEM_ALARM);
    assert(ui_quick_menu_row_item(&state, 2U) == UI_QUICK_ITEM_BRIGHTNESS);
    assert(ui_quick_menu_row_item(&state, 3U) == UI_QUICK_ITEM_BT_OUTPUT);

    /* Walking the list twice round moves the cursor and nothing else: with
       every function on a row there is nothing to scroll. */
    for (int step = 0; step < 2 * (int)UI_QUICK_ITEM_COUNT; ++step) {
        assert(ui_quick_menu_cursor_row(&state) == ui_quick_menu_position(&state));
        assert(ui_quick_menu_row_item(&state, ui_quick_menu_cursor_row(&state)) == state.item);
        assert(ui_quick_menu_row_item(&state, 0U) == UI_QUICK_ITEM_SLEEP);
        (void)ui_quick_menu_handle(&state, BOARD_INPUT_ACTION_ENCODER_RIGHT, 1100U);
    }
}

/* The invariant that has to hold however many rows and functions there are, so
   that a fifth function added later - which would scroll - is still drawn with
   the cursor on it. */
static void test_the_cursor_is_always_on_a_drawn_row(void)
{
    ui_quick_menu_t state;
    open_with_all_rows(&state);
    for (int direction = 0; direction < 2; ++direction) {
        const board_input_action_t turn = direction == 0 ? BOARD_INPUT_ACTION_ENCODER_RIGHT
                                                        : BOARD_INPUT_ACTION_ENCODER_LEFT;
        for (int step = 0; step < 3 * (int)UI_QUICK_ITEM_COUNT; ++step) {
            const uint8_t row = ui_quick_menu_cursor_row(&state);
            assert(row < (uint8_t)UI_QUICK_ROWS);
            assert(ui_quick_menu_row_item(&state, row) == state.item);
            (void)ui_quick_menu_handle(&state, turn, 1200U);
        }
    }
}

/* Fewer functions than rows: the spare rows are empty, and nothing pretends
   otherwise - ui.c draws the window only as tall as the rows in use. A board
   with no Bluetooth module is exactly this. */
static void test_a_short_list_leaves_rows_empty(void)
{
    ui_quick_menu_t state;
    ui_quick_menu_init(&state);
    assert(ui_quick_menu_visible_count(&state) == 2U);
    assert(ui_quick_menu_open(&state, 100U));
    assert(ui_quick_menu_row_item(&state, 0U) == UI_QUICK_ITEM_SLEEP);
    assert(ui_quick_menu_row_item(&state, 1U) == UI_QUICK_ITEM_ALARM);
    for (uint8_t row = 2U; row < (uint8_t)UI_QUICK_ROWS; ++row) {
        assert(ui_quick_menu_row_item(&state, row) == UI_QUICK_ITEM_COUNT);
    }
    /* Past the end of the window, and out of range. */
    assert(ui_quick_menu_row_item(&state, (uint8_t)UI_QUICK_ROWS) == UI_QUICK_ITEM_COUNT);
    assert(ui_quick_menu_row_item(NULL, 0U) == UI_QUICK_ITEM_COUNT);

    /* The module answering adds its row without moving the cursor off the row
       somebody is looking at. */
    (void)ui_quick_menu_handle(&state, BOARD_INPUT_ACTION_ENCODER_RIGHT, 200U);
    assert(state.item == UI_QUICK_ITEM_ALARM);
    ui_quick_menu_set_visible(&state, UI_QUICK_ITEM_BT_OUTPUT, true);
    assert(state.item == UI_QUICK_ITEM_ALARM);
    assert(ui_quick_menu_cursor_row(&state) == 1U);
    assert(ui_quick_menu_row_item(&state, 2U) == UI_QUICK_ITEM_BT_OUTPUT);

    /* And it going quiet again takes its row away, leaving the cursor where it
       was: the row above it did not move. */
    ui_quick_menu_set_visible(&state, UI_QUICK_ITEM_BT_OUTPUT, false);
    assert(state.item == UI_QUICK_ITEM_ALARM);
    assert(ui_quick_menu_cursor_row(&state) == 1U);
    assert(ui_quick_menu_row_item(&state, 2U) == UI_QUICK_ITEM_COUNT);
}

/* Which rows are switches: the two that are, are, and the two that carry a
   number are not - the panel draws one or the other and they share the row's
   right edge. */
static void test_the_switch_rows_are_the_two_switches(void)
{
    assert(ui_quick_item_is_switch(UI_QUICK_ITEM_ALARM));
    assert(ui_quick_item_is_switch(UI_QUICK_ITEM_BT_OUTPUT));
    assert(!ui_quick_item_is_switch(UI_QUICK_ITEM_SLEEP));
    assert(!ui_quick_item_is_switch(UI_QUICK_ITEM_BRIGHTNESS));
    /* Not a row at all, so not a switch either - the window asks this about the
       empty rows too. */
    assert(!ui_quick_item_is_switch(UI_QUICK_ITEM_COUNT));
}

static void test_the_sleep_row_is_a_ring(void)
{
    /* Up through the list and round to off, which is what makes it a ring
       rather than a range with an edge to fall off. */
    assert(ui_quick_sleep_step(0U, 1) == 15U);
    assert(ui_quick_sleep_step(15U, 1) == 30U);
    assert(ui_quick_sleep_step(45U, 1) == 60U);
    assert(ui_quick_sleep_step(60U, 1) == 90U);
    assert(ui_quick_sleep_step(120U, 1) == 0U);

    assert(ui_quick_sleep_step(0U, -1) == 120U);
    assert(ui_quick_sleep_step(15U, -1) == 0U);
    assert(ui_quick_sleep_step(120U, -1) == 90U);

    /* A length that is not on the list - the web page can arm any minute count
       - is answered with the nearest step in that direction, not snapped. */
    assert(ui_quick_sleep_step(20U, 1) == 30U);
    assert(ui_quick_sleep_step(20U, -1) == 15U);
    assert(ui_quick_sleep_step(600U, 1) == 0U);
    assert(ui_quick_sleep_step(600U, -1) == 120U);

    /* And the list itself is the web page's, in order and starting at off. */
    assert(ui_quick_sleep_choices[0] == 0U);
    for (unsigned int index = 1U; index < UI_QUICK_SLEEP_CHOICE_COUNT; ++index) {
        assert(ui_quick_sleep_choices[index] > ui_quick_sleep_choices[index - 1U]);
    }
}

static void test_every_row_is_named_in_both_languages(void)
{
    for (unsigned int item = 0U; item < (unsigned int)UI_QUICK_ITEM_COUNT; ++item) {
        for (int language = 0; language < 2; ++language) {
            const char *label = ui_quick_item_label((ui_quick_item_t)item,
                                                    language == 0 ? DEVICE_LANGUAGE_RU
                                                                  : DEVICE_LANGUAGE_EN);
            assert(label != NULL);
            assert(label[0] != '\0');
            assert(strcmp(label, "?") != 0);
        }
    }
}

static void test_null_is_survivable(void)
{
    assert(ui_quick_menu_handle(NULL, BOARD_INPUT_ACTION_QUICK_MENU, 0U) ==
           UI_QUICK_RESULT_NONE);
    assert(!ui_quick_menu_open(NULL, 0U));
    assert(!ui_quick_menu_idle_expired(NULL, 0U));
    assert(ui_quick_menu_visible_count(NULL) == 0U);
    assert(!ui_quick_menu_item_visible(NULL, UI_QUICK_ITEM_SLEEP));
    ui_quick_menu_init(NULL);
    ui_quick_menu_close(NULL);
    ui_quick_menu_set_visible(NULL, UI_QUICK_ITEM_SLEEP, true);
}

int main(void)
{
    test_the_button_opens_and_closes();
    test_the_knob_walks_the_rows_and_wraps();
    test_the_click_hands_the_knob_to_the_value();
    test_holding_and_the_other_buttons_close_it();
    test_a_row_that_disappears_takes_the_cursor_with_it();
    test_one_row_left_is_not_a_spin();
    test_a_panel_with_no_rows_does_not_open();
    test_every_function_has_a_row();
    test_the_cursor_is_always_on_a_drawn_row();
    test_a_short_list_leaves_rows_empty();
    test_an_untouched_panel_closes_itself();
    test_the_switch_rows_are_the_two_switches();
    test_the_sleep_row_is_a_ring();
    test_every_row_is_named_in_both_languages();
    test_null_is_survivable();
    printf("ui_quick_menu tests passed\n");
    return 0;
}
