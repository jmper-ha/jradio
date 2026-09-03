#include "ui_buffer_graph.h"

#include <string.h>

void ui_buffer_graph_reset(ui_buffer_graph_t *graph, uint32_t now_ms)
{
    if (graph == NULL) return;
    memset(graph->samples, 0, sizeof(graph->samples));
    graph->count = 0U;
    /* A whole step away, so a strip that has just been emptied does not take
     * its first bar from the moment the stream opened - the buffer is filling
     * then, and that bar would read as a fault for the next twenty seconds. */
    graph->next_ms = now_ms + UI_BUFFER_GRAPH_STEP_MS;
}

bool ui_buffer_graph_step(ui_buffer_graph_t *graph, uint32_t now_ms, uint8_t percent)
{
    if (graph == NULL) return false;
    /* Wrapping subtraction against zero, not `now_ms < next_ms`: the tick
     * count wraps, and a comparison would then hold the strip still for the
     * length of the whole range. */
    if ((int32_t)(now_ms - graph->next_ms) < 0) return false;
    if (percent > 100U) percent = 100U;
    /* Everything shifts one place right and the new bar takes the left. The
     * strip is 28 bytes, so moving it is cheaper than the arithmetic a ring
     * would need at every draw. */
    memmove(&graph->samples[1], &graph->samples[0], sizeof(graph->samples) - 1U);
    graph->samples[0] = percent;
    if (graph->count < UI_BUFFER_GRAPH_BARS) graph->count++;
    /* From the deadline rather than from now, so a late pass does not push the
     * next one late as well and let the strip drift slower than it says. A
     * gap long enough to have missed several - the screen was elsewhere -
     * starts over instead of catching up with a burst of identical bars. */
    graph->next_ms += UI_BUFFER_GRAPH_STEP_MS;
    if ((int32_t)(now_ms - graph->next_ms) >= 0) {
        graph->next_ms = now_ms + UI_BUFFER_GRAPH_STEP_MS;
    }
    return true;
}

bool ui_buffer_graph_bar(const ui_buffer_graph_t *graph, size_t column, uint8_t *percent)
{
    if (graph == NULL || percent == NULL || column >= graph->count) return false;
    *percent = graph->samples[column];
    return true;
}

int ui_buffer_graph_bar_height(uint8_t percent, int track_height)
{
    if (track_height <= 0 || percent == 0U) return 0;
    if (percent > 100U) percent = 100U;
    const int height = (track_height * (int)percent + 50) / 100;
    return height < 1 ? 1 : height;
}
