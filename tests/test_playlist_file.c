#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "file_browser.h"
#include "playlist_file.h"

/* Lines are parsed in place, so each case needs its own writable copy. The
 * reference that comes back points into it and is checked before the next
 * call overwrites it. */
static playlist_file_line_t parse(playlist_file_kind_t kind, const char *text,
                                  const char **reference)
{
    static char line[1200];
    snprintf(line, sizeof(line), "%s", text);
    return playlist_file_read_line(kind, line, reference);
}

static void assert_track(playlist_file_kind_t kind, const char *text, const char *expected)
{
    const char *reference = NULL;
    assert(parse(kind, text, &reference) == PLAYLIST_FILE_LINE_TRACK);
    assert(reference != NULL);
    assert(strcmp(reference, expected) == 0);
}

static void assert_line(playlist_file_kind_t kind, const char *text, playlist_file_line_t expected)
{
    const char *reference = NULL;
    assert(parse(kind, text, &reference) == expected);
    if (expected != PLAYLIST_FILE_LINE_TRACK) assert(reference == NULL);
}

static void test_kind_from_name(void)
{
    assert(playlist_file_kind_from_name("playlist.m3u") == PLAYLIST_FILE_M3U);
    assert(playlist_file_kind_from_name("PLAYLIST.M3U") == PLAYLIST_FILE_M3U);
    // .m3u8 is an .m3u that promises UTF-8, which is the only encoding read.
    assert(playlist_file_kind_from_name("playlist.m3u8") == PLAYLIST_FILE_M3U);
    assert(playlist_file_kind_from_name("Music/playlist.PLS") == PLAYLIST_FILE_PLS);
    assert(playlist_file_kind_from_name("track.mp3") == PLAYLIST_FILE_NONE);
    assert(playlist_file_kind_from_name("playlist") == PLAYLIST_FILE_NONE);
    assert(playlist_file_kind_from_name(NULL) == PLAYLIST_FILE_NONE);
    // A name that is nothing but the extension is a hidden file, not a list.
    assert(playlist_file_kind_from_name(".m3u") == PLAYLIST_FILE_NONE);
    assert(playlist_file_kind_from_name(".pls") == PLAYLIST_FILE_NONE);

    assert(strcmp(playlist_file_kind_name(PLAYLIST_FILE_M3U), "M3U") == 0);
    assert(strcmp(playlist_file_kind_name(PLAYLIST_FILE_PLS), "PLS") == 0);
    assert(strcmp(playlist_file_kind_name(PLAYLIST_FILE_NONE), "") == 0);
}

static void test_m3u_lines(void)
{
    assert_line(PLAYLIST_FILE_M3U, "#EXTM3U", PLAYLIST_FILE_LINE_IGNORED);
    assert_line(PLAYLIST_FILE_M3U, "#EXTINF:-1,14 - La Cumparsita", PLAYLIST_FILE_LINE_IGNORED);
    assert_line(PLAYLIST_FILE_M3U, "", PLAYLIST_FILE_LINE_IGNORED);
    assert_line(PLAYLIST_FILE_M3U, "   \t", PLAYLIST_FILE_LINE_IGNORED);
    // The CR of a file written on Windows, which is most of them.
    assert_track(PLAYLIST_FILE_M3U, "Music/1.mp3\r\n", "Music/1.mp3");
    assert_track(PLAYLIST_FILE_M3U, "  Music/1.mp3  ", "Music/1.mp3");
    assert_track(PLAYLIST_FILE_M3U,
                 "Music/Сборник (Часть 4)/14---la-cumparsita.mp3",
                 "Music/Сборник (Часть 4)/14---la-cumparsita.mp3");

    /* A backslash is an ordinary character in a FAT name, so a Windows-written
     * path left alone would be looked for as one long file name. */
    assert_track(PLAYLIST_FILE_M3U, "Music\\Album\\1.mp3", "Music/Album/1.mp3");
    assert_track(PLAYLIST_FILE_M3U, "./1.mp3", "1.mp3");
    assert_track(PLAYLIST_FILE_M3U, ".\\Music\\1.mp3", "Music/1.mp3");
    assert_track(PLAYLIST_FILE_M3U, "Music//Album///1.mp3", "Music/Album/1.mp3");
    // A playlist may name the whole path; it is already resolved.
    assert_track(PLAYLIST_FILE_M3U, "/usb0/Music/1.mp3", "/usb0/Music/1.mp3");

    // Nothing here can play a stream, and no drive letter is mounted.
    assert_line(PLAYLIST_FILE_M3U, "http://stream.example/live", PLAYLIST_FILE_LINE_UNPLAYABLE);
    assert_line(PLAYLIST_FILE_M3U, "D:\\Music\\1.mp3", PLAYLIST_FILE_LINE_UNPLAYABLE);
    // Climbing out of the volume, however it is spelled.
    assert_line(PLAYLIST_FILE_M3U, "../1.mp3", PLAYLIST_FILE_LINE_UNPLAYABLE);
    assert_line(PLAYLIST_FILE_M3U, "Music/../../1.mp3", PLAYLIST_FILE_LINE_UNPLAYABLE);
    assert_line(PLAYLIST_FILE_M3U, "..", PLAYLIST_FILE_LINE_UNPLAYABLE);
    // A trailing slash names a directory, which is not a track.
    assert_line(PLAYLIST_FILE_M3U, "Music/", PLAYLIST_FILE_LINE_UNPLAYABLE);
    // "./" on its own leaves nothing behind to open.
    assert_line(PLAYLIST_FILE_M3U, "./", PLAYLIST_FILE_LINE_IGNORED);
}

static void test_pls_lines(void)
{
    assert_line(PLAYLIST_FILE_PLS, "[playlist]", PLAYLIST_FILE_LINE_IGNORED);
    assert_line(PLAYLIST_FILE_PLS, "NumberOfEntries=6", PLAYLIST_FILE_LINE_IGNORED);
    assert_line(PLAYLIST_FILE_PLS, "Version=2", PLAYLIST_FILE_LINE_IGNORED);
    assert_line(PLAYLIST_FILE_PLS, "Title1=04-pesnya-o-volge", PLAYLIST_FILE_LINE_IGNORED);
    assert_line(PLAYLIST_FILE_PLS, "Length1=-1", PLAYLIST_FILE_LINE_IGNORED);
    // A bare path is an .m3u line; in a .pls it names no field.
    assert_line(PLAYLIST_FILE_PLS, "Music/1.mp3", PLAYLIST_FILE_LINE_IGNORED);

    assert_track(PLAYLIST_FILE_PLS, "File1=1.mp3", "1.mp3");
    assert_track(PLAYLIST_FILE_PLS, "FILE12=Album/1.mp3\r\n", "Album/1.mp3");
    assert_track(PLAYLIST_FILE_PLS, "file3= Album\\1.mp3", "Album/1.mp3");
    assert_track(PLAYLIST_FILE_PLS,
                 "File4=Танцуем слушаем поем/Танцуем слушаем поем-1987.14.02.mp3",
                 "Танцуем слушаем поем/Танцуем слушаем поем-1987.14.02.mp3");
    assert_line(PLAYLIST_FILE_PLS, "File1=http://stream.example/live",
                PLAYLIST_FILE_LINE_UNPLAYABLE);

    // The key is "File" and a number, and nothing else is one.
    assert_line(PLAYLIST_FILE_PLS, "File=1.mp3", PLAYLIST_FILE_LINE_IGNORED);
    assert_line(PLAYLIST_FILE_PLS, "Filename=1.mp3", PLAYLIST_FILE_LINE_IGNORED);
    assert_line(PLAYLIST_FILE_PLS, "Fil=1.mp3", PLAYLIST_FILE_LINE_IGNORED);
    assert_line(PLAYLIST_FILE_PLS, "F", PLAYLIST_FILE_LINE_IGNORED);
    assert_line(PLAYLIST_FILE_PLS, "File1", PLAYLIST_FILE_LINE_IGNORED);
    assert_line(PLAYLIST_FILE_PLS, "File1=", PLAYLIST_FILE_LINE_IGNORED);

    // A kind of NONE parses nothing: it is what an unknown extension gives.
    assert_line(PLAYLIST_FILE_NONE, "File1=1.mp3", PLAYLIST_FILE_LINE_IGNORED);
    assert_line(PLAYLIST_FILE_NONE, "1.mp3", PLAYLIST_FILE_LINE_IGNORED);
}

static void test_reference_is_safe(void)
{
    assert(playlist_file_reference_is_safe("1.mp3"));
    assert(playlist_file_reference_is_safe("Music/Album/1.mp3"));
    assert(playlist_file_reference_is_safe("/usb0/Music/1.mp3"));
    assert(!playlist_file_reference_is_safe(NULL));
    assert(!playlist_file_reference_is_safe(""));
    assert(!playlist_file_reference_is_safe("../1.mp3"));
    assert(!playlist_file_reference_is_safe("Music/../../1.mp3"));
    assert(!playlist_file_reference_is_safe("./1.mp3"));
    assert(!playlist_file_reference_is_safe("Music//1.mp3"));
    assert(!playlist_file_reference_is_safe("C:/Music/1.mp3"));
    assert(!playlist_file_reference_is_safe("http://stream.example/live"));
}

/* The two playlists on the test drive, each relative to its own file: the .m3u
 * in the root writes paths from the root, the .pls inside Music/ writes them
 * from Music/. Both have to resolve to the same track. */
static void test_paths_from_a_playlist(void)
{
    file_browser_entry_t storage[8];
    file_browser_dir_t dir;
    char path[FILE_BROWSER_PATH_MAX_LEN];

    file_browser_dir_init_playlist(&dir, storage, 8U, "/usb0/playlist.m3u");
    assert(file_browser_dir_path_for(&dir, "Music/Album/1.mp3", path, sizeof(path)));
    assert(strcmp(path, "/usb0/Music/Album/1.mp3") == 0);

    file_browser_dir_init_playlist(&dir, storage, 8U, "/usb0/Music/playlist.pls");
    assert(file_browser_dir_path_for(&dir, "Album/1.mp3", path, sizeof(path)));
    assert(strcmp(path, "/usb0/Music/Album/1.mp3") == 0);

    // A reference that is already a full path is used as it stands.
    assert(file_browser_dir_path_for(&dir, "/sd0/1.mp3", path, sizeof(path)));
    assert(strcmp(path, "/sd0/1.mp3") == 0);

    /* Checked again here, not only where the line was parsed: this is where a
     * reference becomes a path something is opened at, and the playlist is a
     * file somebody else wrote. */
    assert(!file_browser_dir_path_for(&dir, "../../1.mp3", path, sizeof(path)));
    assert(!file_browser_dir_path_for(&dir, "http://stream.example/live", path, sizeof(path)));

    // Refused rather than truncated: a truncated path opens the wrong file.
    char small[12];
    assert(!file_browser_dir_path_for(&dir, "Album/1.mp3", small, sizeof(small)));

    // A directory listing joins a name to the directory, as it always did.
    file_browser_dir_init(&dir, storage, 8U, "/usb0/Music");
    assert(file_browser_dir_path_for(&dir, "1.mp3", path, sizeof(path)));
    assert(strcmp(path, "/usb0/Music/1.mp3") == 0);
    // And there a name with a slash in it is not a name at all.
    assert(!file_browser_dir_path_for(&dir, "Album/1.mp3", path, sizeof(path)));
}

/* A playlist listing keeps the order the file gives, which is the order the
 * tracks play in - the one listing that is never sorted. */
static void test_playlist_listing_keeps_its_order(void)
{
    file_browser_entry_t storage[8];
    file_browser_dir_t dir;
    file_browser_dir_init_playlist(&dir, storage, 8U, "/usb0/playlist.m3u");
    assert(file_browser_dir_add(&dir, "Music/z.mp3", FILE_BROWSER_ENTRY_FILE));
    assert(file_browser_dir_add(&dir, "a.flac", FILE_BROWSER_ENTRY_FILE));
    assert(file_browser_dir_add(&dir, "Music/m.mp3", FILE_BROWSER_ENTRY_FILE));
    assert(file_browser_dir_count(&dir) == 3U);
    assert(strcmp(file_browser_dir_entry(&dir, 0U)->name, "Music/z.mp3") == 0);
    assert(strcmp(file_browser_dir_entry(&dir, 1U)->name, "a.flac") == 0);
    assert(strcmp(file_browser_dir_entry(&dir, 2U)->name, "Music/m.mp3") == 0);

    // The row shows the track, not the folders leading to it.
    assert(strcmp(file_browser_display_name("Music/z.mp3"), "z.mp3") == 0);
    assert(strcmp(file_browser_display_name("a.flac"), "a.flac") == 0);
    assert(strcmp(file_browser_display_name(NULL), "") == 0);

    /* A track is found again by the name it has in the listing - the reference
     * as written - which is what puts the browser back on the playing row. */
    assert(file_browser_dir_find(&dir, "Music/m.mp3") == 2U);
    assert(file_browser_dir_find(&dir, "m.mp3") == 3U);
}

int main(void)
{
    test_kind_from_name();
    test_m3u_lines();
    test_pls_lines();
    test_reference_is_safe();
    test_paths_from_a_playlist();
    test_playlist_listing_keeps_its_order();
    printf("playlist_file tests passed\n");
    return 0;
}
