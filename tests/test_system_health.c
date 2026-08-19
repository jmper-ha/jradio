#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "system_health.h"

static void test_the_first_sample_sets_the_watermark(void)
{
    system_health_t health;
    system_health_init(&health);

    /* A comfortable first reading is a watermark, not an alarm. */
    assert(!system_health_note_stack(&health, "ui", 4096U));
    assert(health.task_count == 1U);
    assert(health.tasks[0].stack_free == 4096U);
    assert(health.tasks[0].stack_min_free == 4096U);
}

static void test_only_a_new_worst_below_the_line_warns(void)
{
    /* The point of the once-per-descent rule: a task that simply sits low
     * must not reprint the same warning every cycle for the life of the
     * device, or the log stops being read. */
    system_health_t health;
    system_health_init(&health);

    assert(!system_health_note_stack(&health, "player_control", 2048U));
    /* Falling, but still with room. */
    assert(!system_health_note_stack(&health, "player_control", 1024U));
    /* First crossing warns. */
    assert(system_health_note_stack(&health, "player_control", 400U));
    /* Same depth again says nothing new. */
    assert(!system_health_note_stack(&health, "player_control", 400U));
    /* Nor does recovering. */
    assert(!system_health_note_stack(&health, "player_control", 900U));
    /* A deeper low is worth saying again. */
    assert(system_health_note_stack(&health, "player_control", 128U));
    assert(health.tasks[0].stack_min_free == 128U);
    /* The newest reading stays visible next to the worst one. */
    assert(health.tasks[0].stack_free == 128U);
}

static void test_tasks_are_tracked_separately(void)
{
    system_health_t health;
    system_health_init(&health);

    assert(!system_health_note_stack(&health, "ui", 3000U));
    assert(!system_health_note_stack(&health, "radio_decode", 5000U));
    assert(!system_health_note_stack(&health, "ui", 2500U));
    assert(health.task_count == 2U);
    assert(health.tasks[0].stack_min_free == 2500U);
    assert(health.tasks[1].stack_min_free == 5000U);

    /* Each task keeps its own worst reading, and recovering later must not
     * raise it back - the report is about how close to the edge a stack has
     * ever been, not where it happens to be this minute. */
    assert(!system_health_note_stack(&health, "radio_decode", 1000U));
    assert(!system_health_note_stack(&health, "radio_decode", 9000U));
    assert(health.tasks[1].stack_min_free == 1000U);
    assert(health.tasks[1].stack_free == 9000U);
    assert(health.tasks[0].stack_min_free == 2500U);
}

static void test_a_long_task_name_is_kept_not_dropped(void)
{
    /* FreeRTOS names can fill the field. A truncated name still identifies the
     * task; losing the measurement to a formatting limit would not. */
    system_health_t health;
    system_health_init(&health);

    assert(!system_health_note_stack(&health, "a_very_long_task_name_indeed", 2048U));
    assert(health.task_count == 1U);
    assert(strlen(health.tasks[0].name) == SYSTEM_HEALTH_NAME_MAX - 1U);
    /* And it still matches itself on the next cycle rather than adding a
     * second slot every minute. */
    assert(!system_health_note_stack(&health, "a_very_long_task_name_indeed", 1024U));
    assert(health.task_count == 1U);
    assert(health.tasks[0].stack_min_free == 1024U);
}

static void test_the_task_table_cannot_overflow(void)
{
    system_health_t health;
    system_health_init(&health);

    char name[8];
    for (unsigned int index = 0U; index < SYSTEM_HEALTH_TASK_MAX + 4U; ++index) {
        snprintf(name, sizeof(name), "t%u", index);
        (void)system_health_note_stack(&health, name, 1024U);
    }
    assert(health.task_count == SYSTEM_HEALTH_TASK_MAX);
}

static void test_the_heap_alarm_is_about_the_largest_block(void)
{
    /* Free bytes and the largest block fail differently: TLS needs a
     * contiguous DMA-capable allocation, so a heap with plenty free and no
     * block left is the one that stops streams connecting. */
    system_health_t health;
    system_health_init(&health);

    assert(!system_health_note_heap(&health, 40000U, 20000U));
    assert(health.internal_min_free == 40000U);
    assert(health.internal_min_largest == 20000U);

    /* Fragmenting while the total barely moves is exactly the case to catch. */
    assert(system_health_note_heap(&health, 39000U, 4096U));
    assert(health.internal_min_free == 39000U);
    assert(health.internal_min_largest == 4096U);

    /* Same low block again is not news. */
    assert(!system_health_note_heap(&health, 39000U, 4096U));
    /* Recovering is not either, and must not raise the watermark back. */
    assert(!system_health_note_heap(&health, 50000U, 30000U));
    assert(health.internal_min_largest == 4096U);
    assert(health.internal_min_free == 39000U);
}

static void test_null_arguments_are_refused(void)
{
    system_health_t health;
    system_health_init(&health);
    system_health_init(NULL);
    assert(!system_health_note_stack(NULL, "ui", 100U));
    assert(!system_health_note_stack(&health, NULL, 100U));
    assert(!system_health_note_stack(&health, "", 100U));
    assert(!system_health_note_heap(NULL, 1U, 1U));
}

int main(void)
{
    test_the_first_sample_sets_the_watermark();
    test_only_a_new_worst_below_the_line_warns();
    test_tasks_are_tracked_separately();
    test_a_long_task_name_is_kept_not_dropped();
    test_the_task_table_cannot_overflow();
    test_the_heap_alarm_is_about_the_largest_block();
    test_null_arguments_are_refused();
    puts("system_health tests passed");
    return 0;
}
