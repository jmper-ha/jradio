#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "dlna_browse_stack.h"

/* Going up. A server's tree is addressed by opaque ids, so unlike a file
   browser there is nothing in "where I am" that says where I came from - the
   trail is the only way back, and the only place the heading's text lives. */

static dlna_entry_t make_container(const char *id, const char *title)
{
    dlna_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.kind = DLNA_ENTRY_CONTAINER;
    snprintf(entry.id, sizeof(entry.id), "%s", id);
    snprintf(entry.title, sizeof(entry.title), "%s", title);
    return entry;
}

/* The same thing where a pointer is wanted. One row at a time is all any of
   these tests needs, and the stack copies what it is given. */
static const dlna_entry_t *container(const char *id, const char *title)
{
    static dlna_entry_t entry;
    entry = make_container(id, title);
    return &entry;
}

static void test_it_starts_at_the_root_under_the_servers_name(void)
{
    dlna_browse_stack_t stack;
    dlna_browse_stack_reset(&stack, "Plex Media Server: nas4free");
    /* "0" is what a server calls its root, and it is an id, not a heading. */
    assert(strcmp(dlna_browse_stack_id(&stack), "0") == 0);
    assert(strcmp(dlna_browse_stack_title(&stack), "Plex Media Server: nas4free") == 0);
    assert(dlna_browse_stack_at_root(&stack));
    /* Nothing above the root: this is what tells the browser to close rather
       than to browse again. */
    assert(!dlna_browse_stack_leave(&stack));
    assert(dlna_browse_stack_at_root(&stack));
}

static void test_it_walks_the_tree_this_server_has_and_back_out(void)
{
    /* The real path to a track here: root, Music, a library, "By Album", the
       album. Five levels, which is why one remembered parent would not do. */
    dlna_browse_stack_t stack;
    dlna_browse_stack_reset(&stack, "Plex");

    assert(dlna_browse_stack_enter(&stack, container("abe6121c", "Music")));
    assert(dlna_browse_stack_enter(&stack, container("05356467", "\xd0\x9c\xd1\x83\xd0\xb7\xd1\x8b\xd0\xba\xd0\xb0")));
    assert(dlna_browse_stack_enter(&stack, container("c192fa30", "By Album")));
    assert(dlna_browse_stack_enter(&stack, container("92914b5b", "KISS - Alive! (2007)")));

    assert(strcmp(dlna_browse_stack_id(&stack), "92914b5b") == 0);
    assert(strcmp(dlna_browse_stack_title(&stack), "KISS - Alive! (2007)") == 0);
    assert(!dlna_browse_stack_at_root(&stack));

    /* Back out, one level at a time, each landing under the right heading. */
    assert(dlna_browse_stack_leave(&stack));
    assert(strcmp(dlna_browse_stack_title(&stack), "By Album") == 0);
    assert(strcmp(dlna_browse_stack_id(&stack), "c192fa30") == 0);
    assert(dlna_browse_stack_leave(&stack));
    assert(strcmp(dlna_browse_stack_title(&stack), "\xd0\x9c\xd1\x83\xd0\xb7\xd1\x8b\xd0\xba\xd0\xb0") == 0);
    assert(dlna_browse_stack_leave(&stack));
    assert(strcmp(dlna_browse_stack_title(&stack), "Music") == 0);
    assert(dlna_browse_stack_leave(&stack));
    assert(dlna_browse_stack_at_root(&stack));
    assert(strcmp(dlna_browse_stack_title(&stack), "Plex") == 0);
    assert(!dlna_browse_stack_leave(&stack));
}

static void test_what_cannot_be_opened_leaves_it_where_it_was(void)
{
    dlna_browse_stack_t stack;
    dlna_browse_stack_reset(&stack, "Plex");
    assert(dlna_browse_stack_enter(&stack, container("abe6121c", "Music")));

    /* A track is not somewhere to go. */
    dlna_entry_t track = make_container("9901821e", "Deuce");
    track.kind = DLNA_ENTRY_ITEM;
    assert(!dlna_browse_stack_enter(&stack, &track));

    /* Nor is a container the server gave no id. */
    assert(!dlna_browse_stack_enter(&stack, container("", "Nameless")));
    assert(!dlna_browse_stack_enter(&stack, NULL));

    /* Every refusal leaves the browser exactly where it was: a half-entered
       level would show one container's listing under another's name. */
    assert(strcmp(dlna_browse_stack_id(&stack), "abe6121c") == 0);
    assert(strcmp(dlna_browse_stack_title(&stack), "Music") == 0);
    assert(dlna_browse_stack_leave(&stack));
    assert(dlna_browse_stack_at_root(&stack));
}

static void test_it_stops_going_deeper_rather_than_running_over(void)
{
    dlna_browse_stack_t stack;
    dlna_browse_stack_reset(&stack, "Deep");
    for (size_t level = 1U; level < DLNA_BROWSE_DEPTH_MAX; ++level) {
        char id[16];
        snprintf(id, sizeof(id), "id%zu", level);
        assert(dlna_browse_stack_enter(&stack, container(id, id)));
    }
    assert(stack.depth == DLNA_BROWSE_DEPTH_MAX);

    /* The floor of the tree as far as this browser is concerned. Refused, and
       still usable - the way back out is unharmed. */
    assert(!dlna_browse_stack_enter(&stack, container("toodeep", "Too deep")));
    assert(stack.depth == DLNA_BROWSE_DEPTH_MAX);
    assert(strcmp(dlna_browse_stack_id(&stack), "id11") == 0);

    size_t levels_left = 0U;
    while (dlna_browse_stack_leave(&stack)) ++levels_left;
    assert(levels_left == DLNA_BROWSE_DEPTH_MAX - 1U);
    assert(dlna_browse_stack_at_root(&stack));
}

static void test_a_long_title_is_cut_but_the_id_it_browses_is_not(void)
{
    /* The heading may lose its tail; the id must not, because a cut id names a
       different object or none at all. Ids that do not fit are refused before
       they reach here - see dlna_didl_parse - so what arrives always fits. */
    dlna_browse_stack_t stack;
    dlna_browse_stack_reset(&stack, "Plex");

    dlna_entry_t entry = make_container("abe6121c-1731-4683-815c-89e1dcd2bf11", "");
    memset(entry.title, 'T', sizeof(entry.title) - 1U);
    entry.title[sizeof(entry.title) - 1U] = '\0';
    assert(dlna_browse_stack_enter(&stack, &entry));
    assert(strcmp(dlna_browse_stack_id(&stack), "abe6121c-1731-4683-815c-89e1dcd2bf11") == 0);
    assert(strlen(dlna_browse_stack_title(&stack)) == DLNA_TITLE_MAX - 1U);
}

static void test_nothing_at_all(void)
{
    /* Never NULL and never a crash: callers issue a browse straight off these,
       so there is nothing for them to check first. */
    dlna_browse_stack_reset(NULL, "x");
    assert(strcmp(dlna_browse_stack_id(NULL), "0") == 0);
    assert(strcmp(dlna_browse_stack_title(NULL), "") == 0);
    assert(dlna_browse_stack_at_root(NULL));
    assert(!dlna_browse_stack_leave(NULL));
    assert(!dlna_browse_stack_enter(NULL, container("a", "b")));

    /* A server that would not say what it is called is still browsable. */
    dlna_browse_stack_t stack;
    dlna_browse_stack_reset(&stack, NULL);
    assert(strcmp(dlna_browse_stack_id(&stack), "0") == 0);
    assert(strcmp(dlna_browse_stack_title(&stack), "") == 0);
}

int main(void)
{
    test_it_starts_at_the_root_under_the_servers_name();
    test_it_walks_the_tree_this_server_has_and_back_out();
    test_what_cannot_be_opened_leaves_it_where_it_was();
    test_it_stops_going_deeper_rather_than_running_over();
    test_a_long_title_is_cut_but_the_id_it_browses_is_not();
    test_nothing_at_all();
    puts("dlna_browse_stack tests passed");
    return 0;
}
