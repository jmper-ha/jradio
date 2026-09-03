#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The buffer reading drawn as a strip of bars instead of a number.
 *
 * The number says how much is held right now; this says whether it has been
 * holding. A dropout is the reading falling, and a fall is exactly what a
 * single figure cannot show - by the time anyone looks at it, it is back.
 *
 * Pure so the walk of the strip is testable off the device: everything here
 * takes `now_ms` rather than reading a clock.
 */

/* One bar every 2 px with a pixel between them, across the width the reading's
 * text used to take. Sized here rather than in the layout files because the
 * count is what decides how much history the strip holds, and that is the
 * same question on any panel: 28 bars at 700 ms is about twenty seconds. */
#define UI_BUFFER_GRAPH_BARS 28U
#define UI_BUFFER_GRAPH_BAR_W 2
#define UI_BUFFER_GRAPH_BAR_GAP 1
#define UI_BUFFER_GRAPH_PITCH (UI_BUFFER_GRAPH_BAR_W + UI_BUFFER_GRAPH_BAR_GAP)
#define UI_BUFFER_GRAPH_W ((int)UI_BUFFER_GRAPH_BARS * UI_BUFFER_GRAPH_PITCH - \
                           UI_BUFFER_GRAPH_BAR_GAP)

/* How often a bar is taken. The user asked for 0.7 s; at 28 bars that is a
 * strip holding 19.6 s, which is long enough to see a station settle after it
 * opens and short enough that the whole strip is about what just happened. */
#define UI_BUFFER_GRAPH_STEP_MS 700U

typedef struct {
    /* Newest first: index 0 is the bar just taken. The strip is drawn left to
     * right, so the newest sits at the left margin and the history walks
     * right, which is the direction the user asked the bars to move. */
    uint8_t samples[UI_BUFFER_GRAPH_BARS];
    /* How many of them have been filled. Below the full count the strip is
     * still filling from the left and the rest of it is empty ground, rather
     * than a row of zeroes claiming the buffer was empty before the station
     * was even open. */
    uint8_t count;
    uint32_t next_ms;
} ui_buffer_graph_t;

/* Empties the strip. Called whenever what it was measuring goes away - a
 * station stopped, a source changed - because the bars of the last stream say
 * nothing about the next one. */
void ui_buffer_graph_reset(ui_buffer_graph_t *graph, uint32_t now_ms);

/* Takes a bar if one is due. True when the strip changed and has to be
 * redrawn, which is once every UI_BUFFER_GRAPH_STEP_MS however often the poll
 * loop calls this. */
bool ui_buffer_graph_step(ui_buffer_graph_t *graph, uint32_t now_ms, uint8_t percent);

/* The bar at `column`, counted from the left. False past what has been filled,
 * which is the column having no bar rather than a bar of zero. */
bool ui_buffer_graph_bar(const ui_buffer_graph_t *graph, size_t column, uint8_t *percent);

/* A bar's height in pixels, given the height of the strip it stands in.
 * Anything above zero is at least one pixel: a buffer that holds something has
 * to look different from one that holds nothing, and that is the difference
 * this strip exists to show. */
int ui_buffer_graph_bar_height(uint8_t percent, int track_height);
