#include <assert.h>
#include <stdio.h>

#include "cover_file.h"

static void test_the_three_extensions_are_covers(void)
{
    assert(cover_file_name_matches("cover.jpg"));
    assert(cover_file_name_matches("cover.jpeg"));
    assert(cover_file_name_matches("cover.png"));
}

static void test_case_is_ignored_everywhere(void)
{
    // The name the drive that motivated this actually carries.
    assert(cover_file_name_matches("Cover.png"));
    assert(cover_file_name_matches("COVER.JPG"));
    assert(cover_file_name_matches("cOvEr.JpEg"));
}

static void test_other_conventions_are_not_guessed_at(void)
{
    /* Each of these is some player's idea of a folder cover, and accepting
     * them means opening a file on every track of every album that has none.
     * If one is wanted, it belongs here as a deliberate line, not as a
     * side-effect of a loose match. */
    assert(!cover_file_name_matches("folder.jpg"));
    assert(!cover_file_name_matches("front.jpg"));
    assert(!cover_file_name_matches("albumart.jpg"));
    assert(!cover_file_name_matches("cover art.jpg"));
}

static void test_the_name_has_to_be_the_whole_name(void)
{
    assert(!cover_file_name_matches("cover.png.bak"));
    assert(!cover_file_name_matches("cover2.png"));
    assert(!cover_file_name_matches("mycover.png"));
    assert(!cover_file_name_matches("cover"));
    assert(!cover_file_name_matches("cover."));
    assert(!cover_file_name_matches("cover.bmp"));
    assert(!cover_file_name_matches("cover.jp"));
}

static void test_short_names_do_not_read_past_their_end(void)
{
    // Each of these is shorter than "cover", so the comparison has to stop at
    // the terminator rather than walk on into whatever follows it.
    assert(!cover_file_name_matches(""));
    assert(!cover_file_name_matches("c"));
    assert(!cover_file_name_matches("cove"));
    assert(!cover_file_name_matches(NULL));
}

int main(void)
{
    test_the_three_extensions_are_covers();
    test_case_is_ignored_everywhere();
    test_other_conventions_are_not_guessed_at();
    test_the_name_has_to_be_the_whole_name();
    test_short_names_do_not_read_past_their_end();
    puts("cover_file tests passed");
    return 0;
}
