#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "usb_browser.h"

static void test_format_from_name(void)
{
    assert(usb_browser_format_from_name("track.mp3") == USB_BROWSER_FORMAT_MP3);
    assert(usb_browser_format_from_name("TRACK.MP3") == USB_BROWSER_FORMAT_MP3);
    assert(usb_browser_format_from_name("track.AaC") == USB_BROWSER_FORMAT_AAC);
    assert(usb_browser_format_from_name("track.adts") == USB_BROWSER_FORMAT_AAC);
    assert(usb_browser_format_from_name("track.flac") == USB_BROWSER_FORMAT_FLAC);
    assert(usb_browser_format_from_name("track.ogg") == USB_BROWSER_FORMAT_OGG_FLAC);
    assert(usb_browser_format_from_name("track.oga") == USB_BROWSER_FORMAT_OGG_FLAC);
    assert(usb_browser_format_from_name("track.wav") == USB_BROWSER_FORMAT_WAV);

    /* AAC in an MP4 container is not what the ADTS backend consumes, so it must
     * not be offered as playable. */
    assert(usb_browser_format_from_name("track.m4a") == USB_BROWSER_FORMAT_NONE);
    assert(usb_browser_format_from_name("cover.jpg") == USB_BROWSER_FORMAT_NONE);
    assert(usb_browser_format_from_name("readme") == USB_BROWSER_FORMAT_NONE);
    assert(usb_browser_format_from_name(NULL) == USB_BROWSER_FORMAT_NONE);

    /* A dot only starts an extension when something precedes it: ".mp3" is a
     * hidden file whose whole name happens to look like an extension. */
    assert(usb_browser_format_from_name(".mp3") == USB_BROWSER_FORMAT_NONE);

    /* Real drives carry names with several dots. */
    assert(usb_browser_format_from_name("Танцуем слушаем поем -1986. декабрь.mp3") ==
           USB_BROWSER_FORMAT_MP3);

    assert(strcmp(usb_browser_format_name(USB_BROWSER_FORMAT_MP3), "MP3") == 0);
    assert(strcmp(usb_browser_format_name(USB_BROWSER_FORMAT_NONE), "") == 0);
}

static void test_hidden_names(void)
{
    assert(usb_browser_name_is_hidden("."));
    assert(usb_browser_name_is_hidden(".."));
    assert(usb_browser_name_is_hidden("._track.mp3"));
    assert(usb_browser_name_is_hidden(".Trashes"));
    assert(usb_browser_name_is_hidden(""));
    assert(usb_browser_name_is_hidden(NULL));
    assert(!usb_browser_name_is_hidden("track.mp3"));
}

static void test_add_and_filter(void)
{
    usb_browser_entry_t storage[8];
    usb_browser_dir_t dir;
    usb_browser_dir_init(&dir, storage, 8U, USB_BROWSER_ROOT_PATH);
    assert(usb_browser_dir_count(&dir) == 0U);
    assert(strcmp(dir.path, "/usb0") == 0);

    assert(usb_browser_dir_add(&dir, "track.mp3", USB_BROWSER_ENTRY_FILE));
    assert(usb_browser_dir_add(&dir, "Album", USB_BROWSER_ENTRY_DIRECTORY));
    /* Filtered out, and not counted as a loss: the user did not lose a track. */
    assert(!usb_browser_dir_add(&dir, "cover.jpg", USB_BROWSER_ENTRY_FILE));
    assert(!usb_browser_dir_add(&dir, ".", USB_BROWSER_ENTRY_DIRECTORY));
    assert(!usb_browser_dir_add(&dir, "..", USB_BROWSER_ENTRY_DIRECTORY));
    assert(!usb_browser_dir_add(&dir, "._track.mp3", USB_BROWSER_ENTRY_FILE));
    assert(usb_browser_dir_count(&dir) == 2U);
    assert(dir.dropped_full == 0U && dir.dropped_long_name == 0U);

    const usb_browser_entry_t *entry = usb_browser_dir_entry(&dir, 0U);
    assert(entry != NULL && strcmp(entry->name, "track.mp3") == 0);
    assert(entry->kind == USB_BROWSER_ENTRY_FILE);
    assert(entry->format == USB_BROWSER_FORMAT_MP3);
    entry = usb_browser_dir_entry(&dir, 1U);
    assert(entry != NULL && entry->kind == USB_BROWSER_ENTRY_DIRECTORY);
    /* Directories carry no format even when the folder name ends in ".mp3". */
    assert(entry->format == USB_BROWSER_FORMAT_NONE);
    assert(usb_browser_dir_entry(&dir, 2U) == NULL);
    assert(usb_browser_dir_entry(NULL, 0U) == NULL);
}

static void test_dropped_entries_are_counted(void)
{
    usb_browser_entry_t storage[2];
    usb_browser_dir_t dir;
    usb_browser_dir_init(&dir, storage, 2U, USB_BROWSER_ROOT_PATH);
    assert(usb_browser_dir_add(&dir, "a.mp3", USB_BROWSER_ENTRY_FILE));
    assert(usb_browser_dir_add(&dir, "b.mp3", USB_BROWSER_ENTRY_FILE));
    assert(!usb_browser_dir_add(&dir, "c.mp3", USB_BROWSER_ENTRY_FILE));
    assert(dir.dropped_full == 1U);
    assert(usb_browser_dir_count(&dir) == 2U);

    /* A name that does not fit is dropped rather than truncated: a truncated
     * name cannot be reopened as a path. */
    char long_name[USB_BROWSER_NAME_MAX_LEN + 8];
    memset(long_name, 'x', sizeof(long_name) - 5U);
    memcpy(long_name + sizeof(long_name) - 5U, ".mp3", 5U);
    usb_browser_dir_init(&dir, storage, 2U, USB_BROWSER_ROOT_PATH);
    assert(!usb_browser_dir_add(&dir, long_name, USB_BROWSER_ENTRY_FILE));
    assert(dir.dropped_long_name == 1U);
    assert(usb_browser_dir_count(&dir) == 0U);

    /* Storage is mandatory; an unbacked listing must refuse, not crash. */
    usb_browser_dir_init(&dir, NULL, 8U, USB_BROWSER_ROOT_PATH);
    assert(!usb_browser_dir_add(&dir, "a.mp3", USB_BROWSER_ENTRY_FILE));
    assert(usb_browser_dir_count(&dir) == 0U);
}

static void test_sort_order(void)
{
    usb_browser_entry_t storage[8];
    usb_browser_dir_t dir;
    usb_browser_dir_init(&dir, storage, 8U, USB_BROWSER_ROOT_PATH);
    assert(usb_browser_dir_add(&dir, "zeta.mp3", USB_BROWSER_ENTRY_FILE));
    assert(usb_browser_dir_add(&dir, "Beta", USB_BROWSER_ENTRY_DIRECTORY));
    assert(usb_browser_dir_add(&dir, "alpha.mp3", USB_BROWSER_ENTRY_FILE));
    assert(usb_browser_dir_add(&dir, "arch", USB_BROWSER_ENTRY_DIRECTORY));
    usb_browser_dir_sort(&dir);

    /* Directories first, then case-insensitive by name. */
    assert(strcmp(usb_browser_dir_entry(&dir, 0U)->name, "alpha.mp3") != 0);
    assert(strcmp(usb_browser_dir_entry(&dir, 0U)->name, "arch") == 0);
    assert(strcmp(usb_browser_dir_entry(&dir, 1U)->name, "Beta") == 0);
    assert(strcmp(usb_browser_dir_entry(&dir, 2U)->name, "alpha.mp3") == 0);
    assert(strcmp(usb_browser_dir_entry(&dir, 3U)->name, "zeta.mp3") == 0);

    usb_browser_dir_sort(NULL);
}

static void test_next_file(void)
{
    usb_browser_entry_t storage[8];
    usb_browser_dir_t dir;
    usb_browser_dir_init(&dir, storage, 8U, USB_BROWSER_ROOT_PATH);
    assert(usb_browser_dir_add(&dir, "Album", USB_BROWSER_ENTRY_DIRECTORY));
    assert(usb_browser_dir_add(&dir, "Second", USB_BROWSER_ENTRY_DIRECTORY));
    assert(usb_browser_dir_add(&dir, "a.mp3", USB_BROWSER_ENTRY_FILE));
    assert(usb_browser_dir_add(&dir, "b.mp3", USB_BROWSER_ENTRY_FILE));
    usb_browser_dir_sort(&dir);

    /* Skips the leading directories, then walks the files and finally reports
     * "no more" as the entry count, which is what auto-advance stops on. */
    assert(usb_browser_dir_next_file(&dir, 0U) == 2U);
    assert(usb_browser_dir_next_file(&dir, 2U) == 2U);
    assert(usb_browser_dir_next_file(&dir, 3U) == 3U);
    assert(usb_browser_dir_next_file(&dir, 4U) == 4U);
    assert(usb_browser_dir_next_file(&dir, 99U) == 4U);

    usb_browser_dir_init(&dir, storage, 8U, USB_BROWSER_ROOT_PATH);
    assert(usb_browser_dir_add(&dir, "Only", USB_BROWSER_ENTRY_DIRECTORY));
    assert(usb_browser_dir_next_file(&dir, 0U) == 1U);
}

static void test_paths(void)
{
    char path[USB_BROWSER_PATH_MAX_LEN];
    assert(usb_browser_path_is_root("/usb0"));
    assert(!usb_browser_path_is_root("/usb0/Album"));
    assert(!usb_browser_path_is_root(NULL));

    assert(usb_browser_path_child("/usb0", "Album", path, sizeof(path)));
    assert(strcmp(path, "/usb0/Album") == 0);

    char child[USB_BROWSER_PATH_MAX_LEN];
    assert(usb_browser_path_child(path, "track.mp3", child, sizeof(child)));
    assert(strcmp(child, "/usb0/Album/track.mp3") == 0);
    assert(usb_browser_path_child("/usb0/Album", "Танцуем.mp3", child, sizeof(child)));
    assert(strcmp(child, "/usb0/Album/Танцуем.mp3") == 0);

    /* Escaping the mount point is refused rather than sanitised. */
    assert(!usb_browser_path_child("/usb0", "..", child, sizeof(child)));
    assert(!usb_browser_path_child("/usb0", "sub/dir", child, sizeof(child)));
    assert(!usb_browser_path_child(NULL, "a", child, sizeof(child)));

    /* Too long to fit is a refusal, never a truncation. "/usb0/Album" needs
     * exactly 12 bytes with its terminator, so 12 fits and 11 must not. */
    char exact[12];
    assert(usb_browser_path_child("/usb0", "Album", exact, sizeof(exact)));
    assert(strcmp(exact, "/usb0/Album") == 0);
    char small[11];
    assert(!usb_browser_path_child("/usb0", "Album", small, sizeof(small)));

    assert(usb_browser_path_parent("/usb0/Album/Deep", child, sizeof(child)));
    assert(strcmp(child, "/usb0/Album") == 0);
    assert(usb_browser_path_parent("/usb0/Album", child, sizeof(child)));
    assert(strcmp(child, "/usb0") == 0);
    /* The root has no parent: there is nothing above the mount point. */
    assert(!usb_browser_path_parent("/usb0", child, sizeof(child)));
    assert(!usb_browser_path_parent(NULL, child, sizeof(child)));
}


static void test_finding_an_entry_by_name(void)
{
    /* Only the name survives a restart, so the remembered track is located
     * this way rather than by the index it used to have. */
    usb_browser_entry_t storage[8];
    usb_browser_dir_t dir;
    usb_browser_dir_init(&dir, storage, 8U, "/usb0");
    assert(usb_browser_dir_add(&dir, "b.mp3", USB_BROWSER_ENTRY_FILE));
    assert(usb_browser_dir_add(&dir, "Album", USB_BROWSER_ENTRY_DIRECTORY));
    assert(usb_browser_dir_add(&dir, "a.mp3", USB_BROWSER_ENTRY_FILE));
    usb_browser_dir_sort(&dir);

    const size_t found = usb_browser_dir_find(&dir, "a.mp3");
    assert(found < usb_browser_dir_count(&dir));
    assert(strcmp(usb_browser_dir_entry(&dir, found)->name, "a.mp3") == 0);
    /* Directories are found too - the caller decides what to do with one. */
    assert(usb_browser_dir_find(&dir, "Album") < usb_browser_dir_count(&dir));

    /* A gone track reports past the end, which is how the caller falls back to
     * the browser rather than playing the wrong file. */
    assert(usb_browser_dir_find(&dir, "gone.mp3") == usb_browser_dir_count(&dir));
    assert(usb_browser_dir_find(&dir, "") == usb_browser_dir_count(&dir));
    assert(usb_browser_dir_find(&dir, NULL) == usb_browser_dir_count(&dir));
    assert(usb_browser_dir_find(NULL, "a.mp3") == 0U);
}

int main(void)
{
    test_format_from_name();
    test_hidden_names();
    test_add_and_filter();
    test_dropped_entries_are_counted();
    test_sort_order();
    test_next_file();
    test_paths();
    test_finding_an_entry_by_name();
    printf("usb_browser tests passed\n");
    return 0;
}
