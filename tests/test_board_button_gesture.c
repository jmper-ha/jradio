#include "board_input.h"

#include <assert.h>
#include <stdio.h>

/* The pair a caller passes in; each button brings its own, which is the whole
 * point of the two arguments. */
static board_input_action_t encoder(board_button_gesture_t *gesture, bool pressed,
                                    uint32_t elapsed_ms)
{
    return board_button_gesture_update(gesture, pressed, elapsed_ms,
                                       BOARD_INPUT_ACTION_ENCODER_BUTTON,
                                       BOARD_INPUT_ACTION_ENCODER_LONG);
}

static board_input_action_t sleep_button(board_button_gesture_t *gesture, bool pressed, uint32_t elapsed_ms)
{
    return board_button_gesture_update(gesture, pressed, elapsed_ms, BOARD_INPUT_ACTION_SLEEP_BUTTON,
                                       BOARD_INPUT_ACTION_SLEEP_LONG);
}

static void test_a_hold_reports_once_and_swallows_the_click(void)
{
    board_button_gesture_t gesture;
    board_button_gesture_init(&gesture);

    assert(encoder(&gesture, true, 5) == BOARD_INPUT_ACTION_NONE);
    assert(encoder(&gesture, true, 795) == BOARD_INPUT_ACTION_NONE);
    assert(encoder(&gesture, true, 5) == BOARD_INPUT_ACTION_ENCODER_LONG);
    /* Held on past the threshold: the hold is not repeated... */
    assert(encoder(&gesture, true, 100) == BOARD_INPUT_ACTION_NONE);
    /* ...and the release that follows it is not a click either. */
    assert(encoder(&gesture, false, 5) == BOARD_INPUT_ACTION_NONE);
}

static void test_a_short_press_reports_on_release(void)
{
    board_button_gesture_t gesture;
    board_button_gesture_init(&gesture);

    assert(encoder(&gesture, true, 5) == BOARD_INPUT_ACTION_NONE);
    assert(encoder(&gesture, false, 5) == BOARD_INPUT_ACTION_ENCODER_BUTTON);
}

/* The same state machine, another button's pair of actions - the sleep button
 * held is what puts the board to sleep, and its short press must stay its
 * own. */
static void test_each_button_reports_its_own_actions(void)
{
    board_button_gesture_t gesture;
    board_button_gesture_init(&gesture);

    assert(sleep_button(&gesture, true, 5) == BOARD_INPUT_ACTION_NONE);
    assert(sleep_button(&gesture, true, 800) == BOARD_INPUT_ACTION_SLEEP_LONG);
    assert(sleep_button(&gesture, false, 5) == BOARD_INPUT_ACTION_NONE);

    assert(sleep_button(&gesture, true, 5) == BOARD_INPUT_ACTION_NONE);
    assert(sleep_button(&gesture, false, 5) == BOARD_INPUT_ACTION_SLEEP_BUTTON);
}

int main(void)
{
    test_a_hold_reports_once_and_swallows_the_click();
    test_a_short_press_reports_on_release();
    test_each_button_reports_its_own_actions();
    puts("board_button_gesture tests passed");
    return 0;
}
