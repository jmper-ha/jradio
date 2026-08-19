#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Watermarks for the resources that fail quietly on this board.
 *
 * Both faults this exists for looked like something else at the time. A task
 * stack that had been shrinking for weeks overflowed the first time the
 * compiler inlined a little more aggressively, and the only evidence was a
 * reboot loop. Internal SRAM ran out of contiguous space and HTTPS streams
 * stopped connecting, which read as a network problem. Neither shows up in a
 * snapshot of the current value - what matters is the worst the value has ever
 * been, which is why this keeps watermarks rather than reporting a reading.
 *
 * Everything here is pure so it can be tested on the host; the ESP-side half
 * that reads the actual figures lives in system_report.c. */

#define SYSTEM_HEALTH_TASK_MAX 12U
#define SYSTEM_HEALTH_NAME_MAX 16U

/* Headroom below which a stack is one unlucky call chain from overflowing.
 *
 * An Xtensa window overflow plus a logging call is already a few hundred
 * bytes, and the interrupt that lands on top of it does not ask first. Half a
 * kilobyte is not a safety margin, it is a warning that there is none. */
#define SYSTEM_HEALTH_STACK_ALARM_BYTES 512U

/* Largest contiguous internal block below which allocations start failing in
 * ways that do not look like memory at all. TLS needs DMA-capable memory,
 * which is internal-only, and when it cannot get a contiguous block the
 * symptom is a stream that will not connect. */
#define SYSTEM_HEALTH_BLOCK_ALARM_BYTES 8192U

typedef struct {
    char name[SYSTEM_HEALTH_NAME_MAX];
    uint32_t stack_free;     /* newest sample, bytes */
    uint32_t stack_min_free; /* least ever seen, bytes */
} system_health_task_t;

typedef struct {
    system_health_task_t tasks[SYSTEM_HEALTH_TASK_MAX];
    size_t task_count;
    uint32_t internal_free;
    uint32_t internal_min_free;
    uint32_t internal_largest;
    uint32_t internal_min_largest;
    bool heap_seen;
} system_health_t;

void system_health_init(system_health_t *health);

/* Folds in one task's remaining headroom, adding the task on first sight.
 *
 * True only when the sample is both alarming and a new worst for that task, so
 * a task that simply sits low warns once on the way down instead of every
 * cycle for the life of the device. Names longer than the field are truncated
 * rather than rejected - a truncated name still identifies the task, and
 * dropping the sample would lose the measurement to a formatting detail. */
bool system_health_note_stack(system_health_t *health, const char *name, uint32_t free_bytes);

/* Folds in a heap reading. True on a new worst that is also below
 * SYSTEM_HEALTH_BLOCK_ALARM_BYTES, on the same once-per-descent rule. */
bool system_health_note_heap(system_health_t *health, uint32_t free_bytes, uint32_t largest_block);
