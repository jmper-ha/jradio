#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A segment that travels back and forth along a bar, for a wait whose length
 * is not knowable in advance.
 *
 * Opening a container on a media server is a request whose answer takes
 * anywhere from a tenth of a second to several, and nothing about it can be
 * expressed as a percentage - there is no denominator. A word on the screen
 * said as much and was not believed: text that does not move is exactly what a
 * frozen device shows. Something that moves is the only honest way to say "the
 * device is alive and still waiting".
 *
 * Pure so that the sweep can be tested against the clock instead of watched,
 * including across the tick counter's wrap - the UI clock is milliseconds in a
 * uint32_t and wraps every 49 days. */

typedef struct {
    /* Both in percent of the bar, `start` < `end`, and the width between them
     * is constant. */
    uint8_t start;
    uint8_t end;
} ui_busy_bar_span_t;

/* How wide the travelling segment is, and how long one pass takes. A quarter
 * of the bar reads as a segment rather than as a bar filling up, and a pass a
 * little under a second is quick enough to look busy without flickering. */
#define UI_BUSY_BAR_SEGMENT 25U
#define UI_BUSY_BAR_SWEEP_MS 900U

ui_busy_bar_span_t ui_busy_bar_span(uint32_t now_ms, uint32_t started_ms);

#ifdef __cplusplus
}
#endif
