#include <assert.h>
#include <stdio.h>

#include "ui_touch_gesture.h"

#define AXIS UI_TOUCH_AXIS_VERTICAL

static ui_touch_event_t step(ui_touch_gesture_t *g, bool down, int x, int y, uint32_t now)
{
    return ui_touch_gesture_step(g, down, x, y, now, AXIS, 30);
}

static void test_a_touch_is_a_tap_where_it_came_down(void)
{
    ui_touch_gesture_t g;
    ui_touch_gesture_init(&g);
    assert(step(&g, false, 0, 0, 0).kind == UI_TOUCH_EVENT_NONE);
    assert(step(&g, true, 100, 200, 10).kind == UI_TOUCH_EVENT_PRESSED);
    // Wandering inside the slop is still the same touch.
    assert(step(&g, true, 110, 210, 50).kind == UI_TOUCH_EVENT_NONE);
    const ui_touch_event_t tap = step(&g, false, 0, 0, 100);
    assert(tap.kind == UI_TOUCH_EVENT_TAP);
    assert(tap.x == 100 && tap.y == 200);
    // Over: a release with nothing down says nothing.
    assert(step(&g, false, 0, 0, 110).kind == UI_TOUCH_EVENT_NONE);
}

static void test_a_resting_hand_is_no_tap(void)
{
    ui_touch_gesture_t g;
    ui_touch_gesture_init(&g);
    (void)step(&g, true, 100, 200, 0);
    assert(step(&g, false, 0, 0, 801).kind == UI_TOUCH_EVENT_NONE);
    (void)step(&g, true, 100, 200, 1000);
    assert(step(&g, false, 0, 0, 1800).kind == UI_TOUCH_EVENT_TAP);
}

static void test_a_drag_counts_whole_steps_from_past_the_slop(void)
{
    ui_touch_gesture_t g;
    ui_touch_gesture_init(&g);
    (void)step(&g, true, 100, 200, 0);
    // Out of the slop: the drag starts here and has gone nowhere yet.
    assert(step(&g, true, 100, 220, 10).kind == UI_TOUCH_EVENT_NONE);
    assert(step(&g, true, 100, 249, 20).kind == UI_TOUCH_EVENT_NONE);
    ui_touch_event_t event = step(&g, true, 100, 250, 30);
    assert(event.kind == UI_TOUCH_EVENT_DRAG && event.steps == 1);
    // A jump counts all of it, and the remainder is kept for the next step.
    event = step(&g, true, 100, 325, 40);
    assert(event.kind == UI_TOUCH_EVENT_DRAG && event.steps == 2);
    assert(step(&g, true, 100, 339, 50).kind == UI_TOUCH_EVENT_NONE);
    event = step(&g, true, 100, 340, 60);
    assert(event.kind == UI_TOUCH_EVENT_DRAG && event.steps == 1);
    // And back, against the axis.
    event = step(&g, true, 100, 280, 70);
    assert(event.kind == UI_TOUCH_EVENT_DRAG && event.steps == -2);
    // A drag let go is no tap, however quick.
    assert(step(&g, false, 0, 0, 80).kind == UI_TOUCH_EVENT_NONE);
}

static void test_the_axis_decides_what_counts(void)
{
    ui_touch_gesture_t g;
    ui_touch_gesture_init(&g);
    (void)ui_touch_gesture_step(&g, true, 100, 100, 0, UI_TOUCH_AXIS_HORIZONTAL, 60);
    (void)ui_touch_gesture_step(&g, true, 120, 100, 10, UI_TOUCH_AXIS_HORIZONTAL, 60);
    // Up and down moves nothing on a carousel.
    assert(ui_touch_gesture_step(&g, true, 120, 300, 20, UI_TOUCH_AXIS_HORIZONTAL, 60).kind ==
           UI_TOUCH_EVENT_NONE);
    const ui_touch_event_t event =
        ui_touch_gesture_step(&g, true, 60, 300, 30, UI_TOUCH_AXIS_HORIZONTAL, 60);
    assert(event.kind == UI_TOUCH_EVENT_DRAG && event.steps == -1);
    // A sideways move makes a touch a drag on a list too: it was no tap.
    ui_touch_gesture_init(&g);
    (void)step(&g, true, 100, 100, 0);
    (void)step(&g, true, 140, 100, 10);
    assert(step(&g, false, 0, 0, 20).kind == UI_TOUCH_EVENT_NONE);
}

static void test_an_ignored_press_says_nothing_until_it_is_over(void)
{
    ui_touch_gesture_t g;
    ui_touch_gesture_init(&g);
    assert(step(&g, true, 100, 100, 0).kind == UI_TOUCH_EVENT_PRESSED);
    ui_touch_gesture_ignore(&g);
    assert(step(&g, true, 100, 300, 10).kind == UI_TOUCH_EVENT_NONE);
    assert(step(&g, true, 100, 400, 20).kind == UI_TOUCH_EVENT_NONE);
    assert(step(&g, false, 0, 0, 30).kind == UI_TOUCH_EVENT_NONE);
    // The next one counts.
    assert(step(&g, true, 100, 100, 40).kind == UI_TOUCH_EVENT_PRESSED);
    assert(step(&g, false, 0, 0, 50).kind == UI_TOUCH_EVENT_TAP);
}

// Down at y, moved 30 px every 10 ms for `moves` passes: 3 px/ms, up the axis.
static uint32_t flick(ui_touch_gesture_t *g, int moves)
{
    uint32_t now = 0U;
    int y = 500;
    (void)step(g, true, 100, y, now);
    for (int i = 0; i <= moves; ++i) {
        y -= 30;
        now += 10U;
        (void)step(g, true, 100, y, now);
    }
    return now;
}

static void test_a_flick_rolls_on_and_slows_to_a_stop(void)
{
    ui_touch_gesture_t g;
    ui_touch_gesture_init(&g);
    uint32_t now = flick(&g, 5);
    assert(step(&g, false, 0, 0, now).kind == UI_TOUCH_EVENT_NONE);
    int total = 0;
    uint32_t first_gap = 0U;
    uint32_t last_gap = 0U;
    uint32_t last_step = now;
    for (int pass = 0; pass < 2000 && g.coasting; ++pass) {
        now += 10U;
        const ui_touch_event_t event = step(&g, false, 0, 0, now);
        if (event.kind == UI_TOUCH_EVENT_DRAG) {
            assert(event.coasting);
            // The way the finger was going.
            assert(event.steps < 0);
            total -= event.steps;
            last_gap = now - last_step;
            if (first_gap == 0U) first_gap = last_gap;
            last_step = now;
        }
    }
    // It stopped by itself, a fair way on, and the steps spread out as it did.
    assert(!g.coasting);
    assert(total >= 5);
    assert(last_gap > first_gap);
    assert(step(&g, false, 0, 0, now + 10U).kind == UI_TOUCH_EVENT_NONE);
}

/* As it goes on a list: the finger is seen quickly for a few passes, then the
 * first row's repaint holds the next look back until after it has gone. */
static void test_a_flick_rolls_on_through_a_repaint(void)
{
    ui_touch_gesture_t g;
    ui_touch_gesture_init(&g);
    (void)step(&g, true, 100, 300, 0);
    (void)step(&g, true, 100, 280, 15);
    (void)step(&g, true, 100, 260, 30);
    (void)step(&g, true, 100, 240, 45);
    (void)step(&g, true, 100, 100, 245);
    assert(step(&g, false, 0, 0, 260).kind == UI_TOUCH_EVENT_NONE);
    assert(g.coasting);
    assert(g.velocity < 0.0f);
    /* It rolls as far in repaints of 200 ms as it would in passes of 10: the
     * speed times the decay, near enough, whatever the panel can draw. */
    int rows = 0;
    uint32_t now = 260U;
    for (int pass = 0; pass < 100 && g.coasting; ++pass) {
        now += 200U;
        const ui_touch_event_t event = step(&g, false, 0, 0, now);
        if (event.kind == UI_TOUCH_EVENT_DRAG) rows -= event.steps;
    }
    assert(!g.coasting);
    // 1 px/ms, the limit, over 450 ms, in rows of 30 px.
    assert(rows >= 12 && rows <= 15);
}

/* The contact rolling off a finger that has barely moved is one fast last point,
 * and it is not counted. */
static void test_a_short_touch_with_a_jump_at_the_lift_does_not_roll_on(void)
{
    ui_touch_gesture_t g;
    ui_touch_gesture_init(&g);
    (void)step(&g, true, 100, 300, 0);
    (void)step(&g, true, 100, 298, 20);
    (void)step(&g, true, 100, 296, 40);
    (void)step(&g, true, 100, 215, 50);
    (void)step(&g, false, 0, 0, 60);
    assert(!g.coasting);
}

static void test_a_placed_finger_does_not_roll_on(void)
{
    ui_touch_gesture_t g;
    ui_touch_gesture_init(&g);
    (void)step(&g, true, 100, 100, 0);
    (void)step(&g, true, 100, 130, 100);
    (void)step(&g, true, 100, 160, 200);
    // Held there a moment before the lift: whatever speed there was has gone.
    for (uint32_t now = 210U; now <= 300U; now += 10U) (void)step(&g, true, 100, 160, now);
    (void)step(&g, false, 0, 0, 310);
    assert(!g.coasting);
}

/* A row brought into place, and the contact rolling off as the finger leaves:
 * the fast last points are that, not a flick. */
static void test_aiming_does_not_roll_on_however_it_ends(void)
{
    ui_touch_gesture_t g;
    ui_touch_gesture_init(&g);
    int y = 300;
    uint32_t now = 0U;
    (void)step(&g, true, 100, y, now);
    while (now < 700U) {
        now += 20U;
        y -= 2;
        (void)step(&g, true, 100, y, now);
    }
    for (int i = 0; i < 3; ++i) {
        now += 10U;
        y -= 30;
        (void)step(&g, true, 100, y, now);
    }
    (void)step(&g, false, 0, 0, now + 10U);
    assert(!g.coasting);
}

/* A slow pull over a short time is no flick either. */
static void test_a_short_slow_touch_does_not_roll_on(void)
{
    ui_touch_gesture_t g;
    ui_touch_gesture_init(&g);
    (void)step(&g, true, 100, 300, 0);
    (void)step(&g, true, 100, 280, 100);
    (void)step(&g, true, 100, 260, 200);
    (void)step(&g, false, 0, 0, 300);
    assert(!g.coasting);
}

static void test_a_press_stops_the_roll_and_is_no_tap(void)
{
    ui_touch_gesture_t g;
    ui_touch_gesture_init(&g);
    const uint32_t now = flick(&g, 5);
    (void)step(&g, false, 0, 0, now);
    assert(g.coasting);
    assert(step(&g, true, 100, 200, now + 20U).kind == UI_TOUCH_EVENT_PRESSED);
    assert(!g.coasting);
    assert(step(&g, false, 0, 0, now + 60U).kind == UI_TOUCH_EVENT_NONE);
    // The next touch is a touch again.
    assert(step(&g, true, 100, 200, now + 100U).kind == UI_TOUCH_EVENT_PRESSED);
    assert(step(&g, false, 0, 0, now + 150U).kind == UI_TOUCH_EVENT_TAP);
}

static void test_stop_coast(void)
{
    ui_touch_gesture_t g;
    ui_touch_gesture_init(&g);
    const uint32_t now = flick(&g, 5);
    (void)step(&g, false, 0, 0, now);
    assert(g.coasting);
    ui_touch_gesture_stop_coast(&g);
    assert(!g.coasting);
    assert(step(&g, false, 0, 0, now + 50U).kind == UI_TOUCH_EVENT_NONE);
    // On a press it changes nothing.
    assert(step(&g, true, 100, 200, now + 60U).kind == UI_TOUCH_EVENT_PRESSED);
    ui_touch_gesture_stop_coast(&g);
    assert(g.down);
}

static void test_row_at(void)
{
    assert(ui_touch_row_at(39, 40, 30, 5) == -1);
    assert(ui_touch_row_at(40, 40, 30, 5) == 0);
    assert(ui_touch_row_at(69, 40, 30, 5) == 0);
    assert(ui_touch_row_at(70, 40, 30, 5) == 1);
    assert(ui_touch_row_at(189, 40, 30, 5) == 4);
    assert(ui_touch_row_at(190, 40, 30, 5) == -1);
    assert(ui_touch_row_at(100, 40, 0, 5) == -1);
}

static void test_a_free_touch_runs_the_way_the_finger_first_went(void)
{
    ui_touch_gesture_t g;
    ui_touch_gesture_init(&g);
    (void)ui_touch_gesture_step(&g, true, 100, 100, 0, UI_TOUCH_AXIS_FREE, 30);
    assert(ui_touch_gesture_axis(&g) == UI_TOUCH_AXIS_VERTICAL);
    (void)ui_touch_gesture_step(&g, true, 120, 105, 10, UI_TOUCH_AXIS_FREE, 30);
    assert(ui_touch_gesture_axis(&g) == UI_TOUCH_AXIS_HORIZONTAL);
    ui_touch_event_t event = ui_touch_gesture_step(&g, true, 136, 105, 20, UI_TOUCH_AXIS_FREE, 16);
    assert(event.kind == UI_TOUCH_EVENT_DRAG && event.steps == 1);
    assert(event.axis == UI_TOUCH_AXIS_HORIZONTAL);
    // And stays that way: this touch has said what it is about.
    event = ui_touch_gesture_step(&g, true, 136, 300, 30, UI_TOUCH_AXIS_FREE, 16);
    assert(event.kind == UI_TOUCH_EVENT_NONE);
    (void)ui_touch_gesture_step(&g, false, 0, 0, 800, UI_TOUCH_AXIS_FREE, 16);

    (void)ui_touch_gesture_step(&g, true, 100, 100, 1000, UI_TOUCH_AXIS_FREE, 30);
    assert(ui_touch_gesture_axis(&g) == UI_TOUCH_AXIS_VERTICAL);
    (void)ui_touch_gesture_step(&g, true, 105, 120, 1010, UI_TOUCH_AXIS_FREE, 30);
    assert(ui_touch_gesture_axis(&g) == UI_TOUCH_AXIS_VERTICAL);
    event = ui_touch_gesture_step(&g, true, 105, 150, 1020, UI_TOUCH_AXIS_FREE, 30);
    assert(event.kind == UI_TOUCH_EVENT_DRAG && event.steps == 1);
    assert(event.axis == UI_TOUCH_AXIS_VERTICAL);
}

static void test_held_still_it_is_a_long_touch_once(void)
{
    ui_touch_gesture_t g;
    ui_touch_gesture_init(&g);
    assert(step(&g, true, 100, 100, 0).kind == UI_TOUCH_EVENT_PRESSED);
    assert(step(&g, true, 103, 102, 700).kind == UI_TOUCH_EVENT_NONE);
    assert(step(&g, true, 103, 102, 800).kind == UI_TOUCH_EVENT_LONG);
    assert(step(&g, true, 103, 102, 900).kind == UI_TOUCH_EVENT_NONE);
    assert(step(&g, false, 0, 0, 1000).kind == UI_TOUCH_EVENT_NONE);
    // Moving first, it is a drag however long it then rests.
    assert(step(&g, true, 100, 100, 2000).kind == UI_TOUCH_EVENT_PRESSED);
    (void)step(&g, true, 100, 130, 2010);
    assert(step(&g, true, 100, 130, 3000).kind == UI_TOUCH_EVENT_NONE);
}

static void test_a_player_drag_is_told_by_where_it_began(void)
{
    assert(ui_touch_player_drag(100, 100, UI_TOUCH_AXIS_HORIZONTAL, 480, 320) ==
           UI_TOUCH_PLAYER_SKIP);
    // Sideways from the volume strip or an edge is still a skip.
    assert(ui_touch_player_drag(450, 100, UI_TOUCH_AXIS_HORIZONTAL, 480, 320) ==
           UI_TOUCH_PLAYER_SKIP);
    assert(ui_touch_player_drag(100, 10, UI_TOUCH_AXIS_HORIZONTAL, 480, 320) ==
           UI_TOUCH_PLAYER_SKIP);
    assert(ui_touch_player_drag(450, 100, UI_TOUCH_AXIS_VERTICAL, 480, 320) ==
           UI_TOUCH_PLAYER_VOLUME);
    assert(ui_touch_player_drag(409, 100, UI_TOUCH_AXIS_VERTICAL, 480, 320) ==
           UI_TOUCH_PLAYER_NOTHING);
    assert(ui_touch_player_drag(100, 10, UI_TOUCH_AXIS_VERTICAL, 480, 320) ==
           UI_TOUCH_PLAYER_QUICK_PANEL);
    assert(ui_touch_player_drag(450, 10, UI_TOUCH_AXIS_VERTICAL, 480, 320) ==
           UI_TOUCH_PLAYER_QUICK_PANEL);
    assert(ui_touch_player_drag(450, 300, UI_TOUCH_AXIS_VERTICAL, 480, 320) ==
           UI_TOUCH_PLAYER_ACTION_STRIP);
    assert(ui_touch_player_drag(100, 150, UI_TOUCH_AXIS_VERTICAL, 480, 320) ==
           UI_TOUCH_PLAYER_NOTHING);
    // Started a little below the top edge it is still the pull.
    assert(ui_touch_player_drag(145, 75, UI_TOUCH_AXIS_VERTICAL, 320, 480) ==
           UI_TOUCH_PLAYER_QUICK_PANEL);
    assert(ui_touch_player_axis(0, 480) == UI_TOUCH_AXIS_VERTICAL);
    assert(ui_touch_player_axis(35, 480) == UI_TOUCH_AXIS_VERTICAL);
    assert(ui_touch_player_axis(36, 480) == UI_TOUCH_AXIS_FREE);
    assert(ui_touch_player_axis(443, 480) == UI_TOUCH_AXIS_FREE);
    assert(ui_touch_player_axis(444, 480) == UI_TOUCH_AXIS_VERTICAL);
}

static void test_only_the_middle_of_the_player_pauses(void)
{
    assert(ui_touch_player_tap_toggles(240, 160, 480, 320));
    assert(ui_touch_player_tap_toggles(120, 80, 480, 320));
    assert(ui_touch_player_tap_toggles(359, 239, 480, 320));
    assert(!ui_touch_player_tap_toggles(119, 160, 480, 320));
    assert(!ui_touch_player_tap_toggles(360, 160, 480, 320));
    assert(!ui_touch_player_tap_toggles(240, 79, 480, 320));
    assert(!ui_touch_player_tap_toggles(240, 240, 480, 320));
}

static void test_a_point_on_the_scrollbar_is_an_item(void)
{
    assert(ui_touch_scrollbar_item(100, 100, 80, 8) == 0);
    assert(ui_touch_scrollbar_item(109, 100, 80, 8) == 0);
    assert(ui_touch_scrollbar_item(110, 100, 80, 8) == 1);
    assert(ui_touch_scrollbar_item(179, 100, 80, 8) == 7);
    // Off either end, the end.
    assert(ui_touch_scrollbar_item(50, 100, 80, 8) == 0);
    assert(ui_touch_scrollbar_item(300, 100, 80, 8) == 7);
    assert(ui_touch_scrollbar_item(120, 100, 0, 8) == 0);
    assert(ui_touch_scrollbar_item(120, 100, 80, 0) == 0);
}

static void test_the_strip_holds_what_the_source_can_do(void)
{
    ui_touch_strip_item_t items[UI_TOUCH_STRIP_MAX_ITEMS];
    // Bluetooth: nothing but the way out.
    assert(ui_touch_strip_items(false, false, false, false, items) == 1U);
    assert(items[0] == UI_TOUCH_STRIP_EXIT);
    // A station.
    assert(ui_touch_strip_items(true, false, false, false, items) == 2U);
    assert(items[0] == UI_TOUCH_STRIP_LIST && items[1] == UI_TOUCH_STRIP_EXIT);
    // A file.
    assert(ui_touch_strip_items(true, true, false, false, items) == 3U);
    assert(items[1] == UI_TOUCH_STRIP_SEEK && items[2] == UI_TOUCH_STRIP_EXIT);
    // The tuner.
    assert(ui_touch_strip_items(true, false, true, false, items) == 3U);
    assert(items[1] == UI_TOUCH_STRIP_TUNE);
    // A track of Yandex's, the fullest.
    assert(ui_touch_strip_items(true, true, false, true, items) == 5U);
    assert(items[2] == UI_TOUCH_STRIP_LIKE && items[3] == UI_TOUCH_STRIP_DISLIKE);
    assert(items[4] == UI_TOUCH_STRIP_EXIT);

    assert(ui_touch_strip_cell(0, 480, 4U) == 0);
    assert(ui_touch_strip_cell(119, 480, 4U) == 0);
    assert(ui_touch_strip_cell(120, 480, 4U) == 1);
    assert(ui_touch_strip_cell(479, 480, 4U) == 3);
    assert(ui_touch_strip_cell(480, 480, 4U) == -1);
    assert(ui_touch_strip_cell(-1, 480, 4U) == -1);
    assert(ui_touch_strip_cell(10, 480, 0U) == -1);
}

int main(void)
{
    test_the_strip_holds_what_the_source_can_do();
    test_a_point_on_the_scrollbar_is_an_item();
    test_only_the_middle_of_the_player_pauses();
    test_held_still_it_is_a_long_touch_once();
    test_a_player_drag_is_told_by_where_it_began();
    test_a_free_touch_runs_the_way_the_finger_first_went();
    test_a_touch_is_a_tap_where_it_came_down();
    test_a_resting_hand_is_no_tap();
    test_a_drag_counts_whole_steps_from_past_the_slop();
    test_the_axis_decides_what_counts();
    test_an_ignored_press_says_nothing_until_it_is_over();
    test_a_flick_rolls_on_and_slows_to_a_stop();
    test_a_flick_rolls_on_through_a_repaint();
    test_a_short_touch_with_a_jump_at_the_lift_does_not_roll_on();
    test_a_placed_finger_does_not_roll_on();
    test_aiming_does_not_roll_on_however_it_ends();
    test_a_short_slow_touch_does_not_roll_on();
    test_a_press_stops_the_roll_and_is_no_tap();
    test_stop_coast();
    test_row_at();
    puts("ui_touch_gesture tests passed");
    return 0;
}
