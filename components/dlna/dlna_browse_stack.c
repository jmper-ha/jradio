#include "dlna_browse_stack.h"

#include <stdio.h>
#include <string.h>

void dlna_browse_stack_reset(dlna_browse_stack_t *stack, const char *server_name)
{
    if (stack == NULL) return;
    memset(stack, 0, sizeof(*stack));
    snprintf(stack->levels[0].id, sizeof(stack->levels[0].id), "%s", DLNA_OBJECT_ID_ROOT);
    snprintf(stack->levels[0].title, sizeof(stack->levels[0].title), "%s",
             server_name != NULL ? server_name : "");
    stack->depth = 1U;
}

bool dlna_browse_stack_enter(dlna_browse_stack_t *stack, const dlna_entry_t *entry)
{
    if (stack == NULL || entry == NULL) return false;
    if (stack->depth == 0U || stack->depth >= DLNA_BROWSE_DEPTH_MAX) return false;
    if (entry->kind != DLNA_ENTRY_CONTAINER) return false;
    if (entry->id[0] == '\0') return false;

    dlna_browse_level_t *level = &stack->levels[stack->depth];
    snprintf(level->id, sizeof(level->id), "%s", entry->id);
    snprintf(level->title, sizeof(level->title), "%s", entry->title);
    ++stack->depth;
    return true;
}

bool dlna_browse_stack_leave(dlna_browse_stack_t *stack)
{
    if (stack == NULL || stack->depth <= 1U) return false;
    --stack->depth;
    return true;
}

const char *dlna_browse_stack_id(const dlna_browse_stack_t *stack)
{
    if (stack == NULL || stack->depth == 0U) return DLNA_OBJECT_ID_ROOT;
    return stack->levels[stack->depth - 1U].id;
}

const char *dlna_browse_stack_title(const dlna_browse_stack_t *stack)
{
    if (stack == NULL || stack->depth == 0U) return "";
    return stack->levels[stack->depth - 1U].title;
}

bool dlna_browse_stack_at_root(const dlna_browse_stack_t *stack)
{
    return stack == NULL || stack->depth <= 1U;
}
