#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ui_station_dial.h"

static void test_two_digits_make_a_number_at_once(void)
{
    ui_station_dial_t dial;
    ui_station_dial_init(&dial);
    assert(ui_station_dial_press(&dial, 2, 38, 1000U) == 0U);
    char text[8];
    ui_station_dial_text(&dial, text, sizeof(text));
    assert(strcmp(text, "2_") == 0);
    assert(ui_station_dial_press(&dial, 2, 38, 1300U) == 22U);
    ui_station_dial_text(&dial, text, sizeof(text));
    assert(text[0] == '\0');
    /* Nothing is left waiting. */
    assert(ui_station_dial_poll(&dial, 38, 9000U) == 0U);
}

static void test_one_digit_waits_and_then_stands_alone(void)
{
    ui_station_dial_t dial;
    ui_station_dial_init(&dial);
    assert(ui_station_dial_press(&dial, 3, 38, 1000U) == 0U);
    assert(ui_station_dial_poll(&dial, 38, 1000U + UI_DIAL_WAIT_MS - 1U) == 0U);
    assert(ui_station_dial_poll(&dial, 38, 1000U + UI_DIAL_WAIT_MS) == 3U);
    assert(ui_station_dial_poll(&dial, 38, 5000U) == 0U);
}

static void test_a_digit_that_cannot_begin_a_longer_number_is_the_number(void)
{
    ui_station_dial_t dial;
    ui_station_dial_init(&dial);
    /* Twelve stations: "3" is station 3, no wait - 30 could not exist. */
    assert(ui_station_dial_press(&dial, 3, 12, 1000U) == 3U);
    /* But "1" waits, since 10, 11 and 12 exist. */
    assert(ui_station_dial_press(&dial, 1, 12, 1000U) == 0U);
    assert(ui_station_dial_press(&dial, 2, 12, 1100U) == 12U);
    /* With nine or fewer every digit is immediate. */
    assert(ui_station_dial_press(&dial, 9, 9, 1000U) == 9U);
}

static void test_what_is_dropped(void)
{
    ui_station_dial_t dial;
    ui_station_dial_init(&dial);
    /* A zero to start with is nothing, and leaves nothing pending. */
    assert(ui_station_dial_press(&dial, 0, 38, 1000U) == 0U);
    assert(!dial.pending);
    /* A number past the list: "3" waits, since 30-38 exist, and "39" is
       nothing - not station 3, not station 9. */
    assert(ui_station_dial_press(&dial, 3, 38, 1000U) == 0U);
    assert(dial.pending);
    assert(ui_station_dial_press(&dial, 9, 38, 1100U) == 0U);
    assert(!dial.pending);
    /* "4" with 38 stations cannot begin 40-49, so it is station 4 at once. */
    assert(ui_station_dial_press(&dial, 4, 38, 2000U) == 4U);
    /* "3" then wait, with three stations: 3. */
    assert(ui_station_dial_press(&dial, 3, 3, 3000U) == 3U);
    /* A digit out of range is ignored. */
    assert(ui_station_dial_press(&dial, 12, 38, 4000U) == 0U);
    assert(ui_station_dial_press(NULL, 1, 38, 4000U) == 0U);
}

static void test_the_wait_survives_the_tick_wrapping(void)
{
    ui_station_dial_t dial;
    ui_station_dial_init(&dial);
    assert(ui_station_dial_press(&dial, 2, 38, 0xFFFFFF00U) == 0U);
    assert(ui_station_dial_poll(&dial, 38, 0xFFFFFF00U + 500U) == 0U);
    assert(ui_station_dial_poll(&dial, 38, 0xFFFFFF00U + UI_DIAL_WAIT_MS) == 2U);
}

int main(void)
{
    test_two_digits_make_a_number_at_once();
    test_one_digit_waits_and_then_stands_alone();
    test_a_digit_that_cannot_begin_a_longer_number_is_the_number();
    test_what_is_dropped();
    test_the_wait_survives_the_tick_wrapping();
    printf("ui_station_dial tests passed\n");
    return 0;
}
