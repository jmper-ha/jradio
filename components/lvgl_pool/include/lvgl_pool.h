#pragma once

#include <stddef.h>

/* Where LVGL's own heap comes from.
 *
 * LVGL's builtin allocator otherwise puts its whole pool - LV_MEM_SIZE, 64 KB
 * here - in a static array, and that array lands in internal DRAM, which is
 * the one thing this board is short of. With it there the 132 KB heap region
 * ran down to 4 KB free with a largest block of 2688, and starting a decoder
 * task - 8 KB of stack, in one piece - failed with ESP_ERR_NO_MEM for the
 * radio and for file playback alike.
 *
 * Named by LV_MEM_POOL_ALLOC in the root CMakeLists, which is why this is a
 * plain one-argument function rather than a macro: a function-like macro does
 * not survive the trip through target_compile_definitions.
 *
 * LVGL keeps its own TLSF arena inside this block, so its many small
 * allocations stay out of the system heap - which is the heap that has to hand
 * out those contiguous 8 KB. */
void *lvgl_pool_alloc(size_t size);
