#include "ui_text_scroll.h"

/* How long the text takes to travel its overhang once. Clamped rather than
 * purely proportional, so neither a two-pixel overhang nor a paragraph gets a
 * silly duration. */
static uint32_t travel_ms(int distance)
{
    if (distance <= 0) return UI_TEXT_SCROLL_TRAVEL_MIN_MS;
    const uint32_t proportional = ((uint32_t)distance * 1000U) / UI_TEXT_SCROLL_SPEED_PX_S;
    if (proportional < UI_TEXT_SCROLL_TRAVEL_MIN_MS) return UI_TEXT_SCROLL_TRAVEL_MIN_MS;
    if (proportional > UI_TEXT_SCROLL_TRAVEL_MAX_MS) return UI_TEXT_SCROLL_TRAVEL_MAX_MS;
    return proportional;
}

/* Negative, because scrolling means drawing the text left of its box. */
static int travelled(int distance, uint32_t elapsed, uint32_t duration)
{
    if (duration == 0U) return -distance;
    if (elapsed >= duration) return -distance;
    return -(int)(((uint32_t)distance * elapsed) / duration);
}

static int overhang(int text_width, int view_width)
{
    const int distance = text_width - view_width;
    return distance > 0 ? distance : 0;
}

uint32_t ui_text_scroll_cycle_ms(ui_text_scroll_mode_t mode, int text_width, int view_width)
{
    const int distance = overhang(text_width, view_width);
    if (distance == 0) return 0U;
    const uint32_t travel = travel_ms(distance);
    if (mode == UI_TEXT_SCROLL_LEFT) {
        return travel + UI_TEXT_SCROLL_LEFT_HOLD_MS + UI_TEXT_SCROLL_REPEAT_MS;
    }
    /* The return trip is a second traversal, which is the whole difference
     * between the two modes in time as well as in looks. */
    return travel + UI_TEXT_SCROLL_BOUNCE_HOLD_MS + travel + UI_TEXT_SCROLL_REPEAT_MS;
}

int ui_text_scroll_offset(ui_text_scroll_mode_t mode, int text_width, int view_width,
                          uint32_t elapsed_ms)
{
    const int distance = overhang(text_width, view_width);
    if (distance == 0) return 0;

    const uint32_t travel = travel_ms(distance);
    const uint32_t cycle = ui_text_scroll_cycle_ms(mode, text_width, view_width);
    /* Modulo rather than a stored phase: a caller that skipped frames lands
     * where the clock says it should, instead of falling behind by whatever it
     * missed. */
    uint32_t t = elapsed_ms % cycle;

    if (t < travel) return travelled(distance, t, travel);
    t -= travel;

    const uint32_t hold = mode == UI_TEXT_SCROLL_LEFT ? UI_TEXT_SCROLL_LEFT_HOLD_MS
                                                      : UI_TEXT_SCROLL_BOUNCE_HOLD_MS;
    if (t < hold) return -distance;
    t -= hold;

    if (mode == UI_TEXT_SCROLL_LEFT) {
        /* No return trip: the hold above ends with the text whole again, and
         * what is left of the cycle is the pause on the start. */
        return 0;
    }
    if (t < travel) return -distance - travelled(distance, t, travel);
    return 0;
}
