#include <assert.h>
#include <stdio.h>

#include "dlna_root_filter.h"

/* What this has to get right is not "does it match a word" but the balance
   between hiding the sections nobody here can play and never hiding a whole
   library because a server named its folders unexpectedly. */

static void test_it_hides_the_sections_a_music_player_cannot_use(void)
{
    /* The three the server on this LAN publishes, verbatim. */
    assert(dlna_root_filter_is_music("Music"));
    assert(!dlna_root_filter_is_music("Video"));
    assert(!dlna_root_filter_is_music("Photos"));

    /* And the names the other common servers use for the same thing. */
    assert(!dlna_root_filter_is_music("Pictures"));
    assert(!dlna_root_filter_is_music("Movies"));
    assert(!dlna_root_filter_is_music("Recorded TV"));
    assert(!dlna_root_filter_is_music("Видео"));
    assert(!dlna_root_filter_is_music("Фото"));
}

static void test_case_and_stray_spaces_do_not_smuggle_a_section_through(void)
{
    assert(!dlna_root_filter_is_music("VIDEO"));
    assert(!dlna_root_filter_is_music("video"));
    assert(!dlna_root_filter_is_music("  Photos  "));
    /* Cyrillic is compared without folding, so both spellings are listed
       rather than derived - the point of this case is that both work. */
    assert(!dlna_root_filter_is_music("видео"));
}

static void test_anything_it_does_not_recognise_stays(void)
{
    /* The deny list is the whole design: a section this does not know about
       is shown, because hiding a user's music is a far worse failure than
       showing them a folder of films they can ignore. */
    assert(dlna_root_filter_is_music("Музыка"));
    assert(dlna_root_filter_is_music("Audio"));
    assert(dlna_root_filter_is_music("Browse Folders"));
    assert(dlna_root_filter_is_music("Медиатека Дениса"));
    assert(dlna_root_filter_is_music(""));
    assert(dlna_root_filter_is_music(NULL));

    /* A name that merely contains one of the words is not that section: an
       album really can be called "Video Games". */
    assert(dlna_root_filter_is_music("Video Games"));
    assert(dlna_root_filter_is_music("Music Videos For Later"));

    /* Longer than any entry in the list, so it is answered without being
       copied - the path that skips the comparison entirely. */
    assert(dlna_root_filter_is_music("A section named far past the buffer this uses"));
}

int main(void)
{
    test_it_hides_the_sections_a_music_player_cannot_use();
    test_case_and_stray_spaces_do_not_smuggle_a_section_through();
    test_anything_it_does_not_recognise_stays();
    printf("dlna_root_filter tests passed\n");
    return 0;
}
