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
 * (F1 on the silkscreen) held is what puts the board to sleep, and its short
 * press must stay its own. */
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

/* A step on the press, nothing more until the delay, then a slow repeat, then
   a fast one; a release starts it over. Called every 5 ms, as the task does. */
static void test_a_held_button_repeats_faster_the_longer_it_is_held(void)
{
    board_button_repeat_t repeat;
    board_button_repeat_init(&repeat);
    assert(!board_button_repeat_update(&repeat, false, 5U));
    assert(board_button_repeat_update(&repeat, true, 5U));
    unsigned steps = 0U;
    unsigned first_repeat_at = 0U;
    for (unsigned held = 5U; held <= 3000U; held += 5U) {
        if (board_button_repeat_update(&repeat, true, 5U)) {
            ++steps;
            if (first_repeat_at == 0U) first_repeat_at = held;
        }
    }
    assert(first_repeat_at == BOARD_BUTTON_REPEAT_DELAY_MS);
    // 400..1500 every 150 ms, then every 60 ms to 3000.
    const unsigned slow = (BOARD_BUTTON_REPEAT_FAST_AFTER_MS - BOARD_BUTTON_REPEAT_DELAY_MS) /
                          BOARD_BUTTON_REPEAT_SLOW_MS + 1U;
    assert(steps >= slow + (3000U - BOARD_BUTTON_REPEAT_FAST_AFTER_MS) / BOARD_BUTTON_REPEAT_FAST_MS - 2U);
    assert(steps <= slow + (3000U - BOARD_BUTTON_REPEAT_FAST_AFTER_MS) / BOARD_BUTTON_REPEAT_FAST_MS + 2U);
    // Let go and pressed again: one step at once, as at first.
    assert(!board_button_repeat_update(&repeat, false, 5U));
    assert(board_button_repeat_update(&repeat, true, 5U));
    assert(!board_button_repeat_update(&repeat, true, 300U));
    assert(!board_button_repeat_update(NULL, true, 5U));
}

int main(void)
{
    test_a_held_button_repeats_faster_the_longer_it_is_held();
    test_a_hold_reports_once_and_swallows_the_click();
    test_a_short_press_reports_on_release();
    test_each_button_reports_its_own_actions();
    puts("board_button_gesture tests passed");
    return 0;
}
