#include <assert.h>
#include <stdio.h>

#include "ui_buffer_graph.h"

static void test_a_fresh_strip_has_no_bars(void)
{
    ui_buffer_graph_t graph;
    ui_buffer_graph_reset(&graph, 1000U);
    uint8_t percent = 200U;
    assert(!ui_buffer_graph_bar(&graph, 0U, &percent));
    assert(percent == 200U);
    /* And it waits a whole step before taking one: the first moment of a
     * stream is the buffer filling, and that bar would stand for twenty
     * seconds looking like a fault. */
    assert(!ui_buffer_graph_step(&graph, 1000U, 50U));
    assert(!ui_buffer_graph_step(&graph, 1000U + UI_BUFFER_GRAPH_STEP_MS - 1U, 50U));
    assert(ui_buffer_graph_step(&graph, 1000U + UI_BUFFER_GRAPH_STEP_MS, 50U));
}

static void test_the_newest_bar_is_the_leftmost_one(void)
{
    /* The user asked for the bars to move right, so a new one arrives at the
     * left margin and the history walks away from it. */
    ui_buffer_graph_t graph;
    ui_buffer_graph_reset(&graph, 0U);
    uint32_t now = 0U;
    for (uint8_t value = 10U; value <= 30U; value += 10U) {
        now += UI_BUFFER_GRAPH_STEP_MS;
        assert(ui_buffer_graph_step(&graph, now, value));
    }
    uint8_t percent = 0U;
    assert(ui_buffer_graph_bar(&graph, 0U, &percent) && percent == 30U);
    assert(ui_buffer_graph_bar(&graph, 1U, &percent) && percent == 20U);
    assert(ui_buffer_graph_bar(&graph, 2U, &percent) && percent == 10U);
    /* Nothing has reached the fourth column yet, which is not the same as a
     * bar of zero standing there. */
    assert(!ui_buffer_graph_bar(&graph, 3U, &percent));
}

static void test_the_strip_holds_only_its_own_width(void)
{
    ui_buffer_graph_t graph;
    ui_buffer_graph_reset(&graph, 0U);
    uint32_t now = 0U;
    for (unsigned int step = 0U; step < UI_BUFFER_GRAPH_BARS + 10U; ++step) {
        now += UI_BUFFER_GRAPH_STEP_MS;
        (void)ui_buffer_graph_step(&graph, now, (uint8_t)(step % 101U));
    }
    uint8_t percent = 0U;
    assert(ui_buffer_graph_bar(&graph, UI_BUFFER_GRAPH_BARS - 1U, &percent));
    assert(!ui_buffer_graph_bar(&graph, UI_BUFFER_GRAPH_BARS, &percent));
}

static void test_a_step_is_taken_once_however_often_it_is_asked(void)
{
    /* The poll loop runs every 10 ms and this is called from it. */
    ui_buffer_graph_t graph;
    ui_buffer_graph_reset(&graph, 0U);
    unsigned int taken = 0U;
    for (uint32_t now = 0U; now <= 7000U; now += 10U) {
        if (ui_buffer_graph_step(&graph, now, 50U)) taken++;
    }
    assert(taken == 10U);
    assert(graph.count == 10U);
}

static void test_a_long_gap_starts_over_rather_than_catching_up(void)
{
    /* The screen was somewhere else for a minute. Eighty-five identical bars
     * would say the buffer sat at that level all along; it says nothing about
     * the time nobody was measuring. */
    ui_buffer_graph_t graph;
    ui_buffer_graph_reset(&graph, 0U);
    assert(ui_buffer_graph_step(&graph, 60000U, 40U));
    assert(graph.count == 1U);
    assert(!ui_buffer_graph_step(&graph, 60000U + UI_BUFFER_GRAPH_STEP_MS - 1U, 40U));
    assert(ui_buffer_graph_step(&graph, 60000U + UI_BUFFER_GRAPH_STEP_MS, 40U));
    assert(graph.count == 2U);
}

static void test_the_step_survives_the_tick_count_wrapping(void)
{
    /* xTaskGetTickCount() wraps, and a plain `now < next` comparison would
     * then hold the strip still for the rest of the range. */
    ui_buffer_graph_t graph;
    const uint32_t before_wrap = 0xFFFFFFFFU - (UI_BUFFER_GRAPH_STEP_MS / 2U);
    ui_buffer_graph_reset(&graph, before_wrap);
    assert(!ui_buffer_graph_step(&graph, before_wrap + 10U, 50U));
    /* Past the wrap, and past the deadline that landed on the other side. */
    assert(ui_buffer_graph_step(&graph, before_wrap + UI_BUFFER_GRAPH_STEP_MS, 50U));
}

static void test_resetting_empties_the_strip(void)
{
    ui_buffer_graph_t graph;
    ui_buffer_graph_reset(&graph, 0U);
    assert(ui_buffer_graph_step(&graph, UI_BUFFER_GRAPH_STEP_MS, 90U));
    ui_buffer_graph_reset(&graph, 10000U);
    uint8_t percent = 0U;
    assert(!ui_buffer_graph_bar(&graph, 0U, &percent));
    assert(graph.count == 0U);
}

static void test_a_bar_is_as_tall_as_its_share_of_the_strip(void)
{
    assert(ui_buffer_graph_bar_height(100U, 12) == 12);
    assert(ui_buffer_graph_bar_height(50U, 12) == 6);
    /* Rounded to nearest: 25% of 12 is 3. */
    assert(ui_buffer_graph_bar_height(25U, 12) == 3);
    /* An empty buffer draws nothing at all - that is the one reading the
     * strip has to be able to show as absence. */
    assert(ui_buffer_graph_bar_height(0U, 12) == 0);
    /* Anything above empty draws something: 1% of 12 rounds to zero, and a
     * buffer holding a little must not look like one holding nothing. */
    assert(ui_buffer_graph_bar_height(1U, 12) == 1);
    assert(ui_buffer_graph_bar_height(3U, 12) == 1);
    /* Degenerate strips and out-of-range readings must not run off it. */
    assert(ui_buffer_graph_bar_height(100U, 0) == 0);
    assert(ui_buffer_graph_bar_height(200U, 12) == 12);
}

static void test_nothing_takes_a_null_strip_down(void)
{
    uint8_t percent = 0U;
    ui_buffer_graph_reset(NULL, 0U);
    assert(!ui_buffer_graph_step(NULL, 1000U, 50U));
    assert(!ui_buffer_graph_bar(NULL, 0U, &percent));

    ui_buffer_graph_t graph;
    ui_buffer_graph_reset(&graph, 0U);
    assert(ui_buffer_graph_step(&graph, UI_BUFFER_GRAPH_STEP_MS, 50U));
    assert(!ui_buffer_graph_bar(&graph, 0U, NULL));
}

static void test_the_strip_is_as_wide_as_its_bars(void)
{
    /* The width is derived so a panel file cannot size the strip out of step
     * with the number of bars drawn into it. */
    assert(UI_BUFFER_GRAPH_W ==
           (int)UI_BUFFER_GRAPH_BARS * UI_BUFFER_GRAPH_PITCH - UI_BUFFER_GRAPH_BAR_GAP);
    assert(UI_BUFFER_GRAPH_W == 83);
}

int main(void)
{
    test_a_fresh_strip_has_no_bars();
    test_the_newest_bar_is_the_leftmost_one();
    test_the_strip_holds_only_its_own_width();
    test_a_step_is_taken_once_however_often_it_is_asked();
    test_a_long_gap_starts_over_rather_than_catching_up();
    test_the_step_survives_the_tick_count_wrapping();
    test_resetting_empties_the_strip();
    test_a_bar_is_as_tall_as_its_share_of_the_strip();
    test_nothing_takes_a_null_strip_down();
    test_the_strip_is_as_wide_as_its_bars();
    puts("ui_buffer_graph tests passed");
    return 0;
}
