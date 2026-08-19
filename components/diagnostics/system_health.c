#include "system_health.h"

#include <string.h>

void system_health_init(system_health_t *health)
{
    if (health == NULL) return;
    memset(health, 0, sizeof(*health));
}

static system_health_task_t *find_or_add(system_health_t *health, const char *name)
{
    for (size_t index = 0U; index < health->task_count; ++index) {
        /* Compared over the stored length, not the field size: the stored name
         * is already truncated, so comparing one byte further would fail to
         * match a long name against its own truncation and add a fresh slot
         * every cycle. Two tasks sharing their first characters are counted as
         * one, which is the same truncation FreeRTOS applies to task names
         * anyway. */
        if (strncmp(health->tasks[index].name, name, SYSTEM_HEALTH_NAME_MAX - 1U) == 0) {
            return &health->tasks[index];
        }
    }
    if (health->task_count == SYSTEM_HEALTH_TASK_MAX) return NULL;

    system_health_task_t *slot = &health->tasks[health->task_count++];
    /* Copied by hand rather than with strncpy, which leaves the name
     * unterminated at exactly the length that fills the field. */
    size_t length = 0U;
    while (length < SYSTEM_HEALTH_NAME_MAX - 1U && name[length] != '\0') {
        slot->name[length] = name[length];
        ++length;
    }
    slot->name[length] = '\0';
    /* So the first sample is always a new worst, without a separate flag. */
    slot->stack_min_free = UINT32_MAX;
    return slot;
}

bool system_health_note_stack(system_health_t *health, const char *name, uint32_t free_bytes)
{
    if (health == NULL || name == NULL || name[0] == '\0') return false;
    system_health_task_t *task = find_or_add(health, name);
    if (task == NULL) return false;

    task->stack_free = free_bytes;
    if (free_bytes >= task->stack_min_free) return false;
    task->stack_min_free = free_bytes;
    return free_bytes < SYSTEM_HEALTH_STACK_ALARM_BYTES;
}

bool system_health_note_heap(system_health_t *health, uint32_t free_bytes, uint32_t largest_block)
{
    if (health == NULL) return false;

    health->internal_free = free_bytes;
    health->internal_largest = largest_block;
    if (!health->heap_seen || free_bytes < health->internal_min_free) {
        health->internal_min_free = free_bytes;
    }
    const bool new_worst_block =
        !health->heap_seen || largest_block < health->internal_min_largest;
    if (new_worst_block) health->internal_min_largest = largest_block;
    health->heap_seen = true;
    return new_worst_block && largest_block < SYSTEM_HEALTH_BLOCK_ALARM_BYTES;
}
