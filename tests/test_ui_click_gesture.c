#include <assert.h>
#include <stdio.h>

#include "ui_click_gesture.h"

#define WINDOW UI_CLICK_DOUBLE_WINDOW_MS

static void test_one_press_becomes_a_single_after_the_window(void)
{
    ui_click_gesture_t gesture;
    ui_click_gesture_init(&gesture);

    /* The press itself answers nothing: at this point it could still turn out
     * to be the first half of a double. */
    assert(ui_click_gesture_press(&gesture, 1000U) == UI_CLICK_NONE);
    assert(ui_click_gesture_is_pending(&gesture));
    assert(ui_click_gesture_poll(&gesture, 1000U + WINDOW - 1U) == UI_CLICK_NONE);
    assert(ui_click_gesture_poll(&gesture, 1000U + WINDOW) == UI_CLICK_SINGLE);
    /* And only once - the poll loop runs every 10 ms. */
    assert(ui_click_gesture_poll(&gesture, 1000U + WINDOW + 100U) == UI_CLICK_NONE);
    assert(!ui_click_gesture_is_pending(&gesture));
}

static void test_a_second_press_inside_the_window_is_a_double(void)
{
    ui_click_gesture_t gesture;
    ui_click_gesture_init(&gesture);

    assert(ui_click_gesture_press(&gesture, 1000U) == UI_CLICK_NONE);
    /* Reported immediately: the action a double click triggers should not wait
     * for a timer that has already told us everything. */
    assert(ui_click_gesture_press(&gesture, 1000U + WINDOW - 1U) == UI_CLICK_DOUBLE);
    /* The pending single must be consumed, or it would fire afterwards and
     * apply both actions to one gesture. */
    assert(!ui_click_gesture_is_pending(&gesture));
    assert(ui_click_gesture_poll(&gesture, 1000U + 10000U) == UI_CLICK_NONE);
}

static void test_a_press_exactly_at_the_window_edge_is_not_a_double(void)
{
    ui_click_gesture_t gesture;
    ui_click_gesture_init(&gesture);

    assert(ui_click_gesture_press(&gesture, 1000U) == UI_CLICK_NONE);
    assert(ui_click_gesture_press(&gesture, 1000U + WINDOW) == UI_CLICK_NONE);
    /* It opened a new window instead, so it becomes a single of its own. */
    assert(ui_click_gesture_poll(&gesture, 1000U + 2U * WINDOW) == UI_CLICK_SINGLE);
}

static void test_two_slow_presses_are_two_singles(void)
{
    ui_click_gesture_t gesture;
    ui_click_gesture_init(&gesture);

    assert(ui_click_gesture_press(&gesture, 1000U) == UI_CLICK_NONE);
    assert(ui_click_gesture_poll(&gesture, 1400U) == UI_CLICK_SINGLE);
    assert(ui_click_gesture_press(&gesture, 2000U) == UI_CLICK_NONE);
    assert(ui_click_gesture_poll(&gesture, 2400U) == UI_CLICK_SINGLE);
}

static void test_a_third_fast_press_starts_over_rather_than_chaining(void)
{
    /* Triple click is not a gesture here. The third press must not silently
     * pair with the second and fire a second double. */
    ui_click_gesture_t gesture;
    ui_click_gesture_init(&gesture);

    assert(ui_click_gesture_press(&gesture, 1000U) == UI_CLICK_NONE);
    assert(ui_click_gesture_press(&gesture, 1100U) == UI_CLICK_DOUBLE);
    assert(ui_click_gesture_press(&gesture, 1200U) == UI_CLICK_NONE);
    assert(ui_click_gesture_poll(&gesture, 1200U + WINDOW) == UI_CLICK_SINGLE);
}

static void test_cancelling_drops_the_pending_click(void)
{
    /* The screen can change under a pending click - a long press leaves for
     * the home screen - and the click must not land there. */
    ui_click_gesture_t gesture;
    ui_click_gesture_init(&gesture);

    assert(ui_click_gesture_press(&gesture, 1000U) == UI_CLICK_NONE);
    ui_click_gesture_cancel(&gesture);
    assert(!ui_click_gesture_is_pending(&gesture));
    assert(ui_click_gesture_poll(&gesture, 1000U + WINDOW) == UI_CLICK_NONE);

    /* And a cancel leaves the detector usable. */
    assert(ui_click_gesture_press(&gesture, 2000U) == UI_CLICK_NONE);
    assert(ui_click_gesture_poll(&gesture, 2000U + WINDOW) == UI_CLICK_SINGLE);
}

static void test_the_window_survives_the_millisecond_counter_wrapping(void)
{
    /* ui_tick_get_ms() is a uint32_t of milliseconds and wraps about every
     * 49 days; a signed comparison here would stall the click for that long. */
    ui_click_gesture_t gesture;
    ui_click_gesture_init(&gesture);

    const uint32_t before_wrap = 0xFFFFFF00U;
    assert(ui_click_gesture_press(&gesture, before_wrap) == UI_CLICK_NONE);
    /* 100 ms later, having wrapped past zero. */
    const uint32_t after_wrap = before_wrap + 100U;
    assert(ui_click_gesture_press(&gesture, after_wrap) == UI_CLICK_DOUBLE);

    ui_click_gesture_init(&gesture);
    assert(ui_click_gesture_press(&gesture, before_wrap) == UI_CLICK_NONE);
    assert(ui_click_gesture_poll(&gesture, before_wrap + WINDOW) == UI_CLICK_SINGLE);
}

static void test_polling_an_idle_detector_answers_nothing(void)
{
    ui_click_gesture_t gesture;
    ui_click_gesture_init(&gesture);
    assert(ui_click_gesture_poll(&gesture, 5000U) == UI_CLICK_NONE);
    assert(!ui_click_gesture_is_pending(&gesture));
}

static void test_the_detector_tolerates_being_handed_nothing(void)
{
    assert(ui_click_gesture_press(NULL, 1000U) == UI_CLICK_NONE);
    assert(ui_click_gesture_poll(NULL, 1000U) == UI_CLICK_NONE);
    assert(!ui_click_gesture_is_pending(NULL));
    ui_click_gesture_cancel(NULL);
    ui_click_gesture_init(NULL);
}

int main(void)
{
    test_one_press_becomes_a_single_after_the_window();
    test_a_second_press_inside_the_window_is_a_double();
    test_a_press_exactly_at_the_window_edge_is_not_a_double();
    test_two_slow_presses_are_two_singles();
    test_a_third_fast_press_starts_over_rather_than_chaining();
    test_cancelling_drops_the_pending_click();
    test_the_window_survives_the_millisecond_counter_wrapping();
    test_polling_an_idle_detector_answers_nothing();
    test_the_detector_tolerates_being_handed_nothing();
    puts("ui_click_gesture tests passed");
    return 0;
}
