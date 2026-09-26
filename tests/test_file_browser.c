#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "file_browser.h"

static void test_format_from_name(void)
{
    assert(file_browser_format_from_name("track.mp3") == FILE_BROWSER_FORMAT_MP3);
    assert(file_browser_format_from_name("TRACK.MP3") == FILE_BROWSER_FORMAT_MP3);
    assert(file_browser_format_from_name("track.AaC") == FILE_BROWSER_FORMAT_AAC);
    assert(file_browser_format_from_name("track.adts") == FILE_BROWSER_FORMAT_AAC);
    assert(file_browser_format_from_name("track.flac") == FILE_BROWSER_FORMAT_FLAC);
    assert(file_browser_format_from_name("track.ogg") == FILE_BROWSER_FORMAT_OGG_FLAC);
    assert(file_browser_format_from_name("track.oga") == FILE_BROWSER_FORMAT_OGG_FLAC);
    assert(file_browser_format_from_name("track.wav") == FILE_BROWSER_FORMAT_WAV);

    /* AAC in an MP4 container is not what the ADTS backend consumes, so it must
     * not be offered as playable. */
    assert(file_browser_format_from_name("track.m4a") == FILE_BROWSER_FORMAT_NONE);
    assert(file_browser_format_from_name("cover.jpg") == FILE_BROWSER_FORMAT_NONE);
    assert(file_browser_format_from_name("readme") == FILE_BROWSER_FORMAT_NONE);
    assert(file_browser_format_from_name(NULL) == FILE_BROWSER_FORMAT_NONE);

    /* A dot only starts an extension when something precedes it: ".mp3" is a
     * hidden file whose whole name happens to look like an extension. */
    assert(file_browser_format_from_name(".mp3") == FILE_BROWSER_FORMAT_NONE);

    /* Real drives carry names with several dots. */
    assert(file_browser_format_from_name("Танцуем слушаем поем -1986. декабрь.mp3") ==
           FILE_BROWSER_FORMAT_MP3);

    assert(strcmp(file_browser_format_name(FILE_BROWSER_FORMAT_MP3), "MP3") == 0);
    assert(strcmp(file_browser_format_name(FILE_BROWSER_FORMAT_NONE), "") == 0);
}

static void test_hidden_names(void)
{
    assert(file_browser_name_is_hidden("."));
    assert(file_browser_name_is_hidden(".."));
    assert(file_browser_name_is_hidden("._track.mp3"));
    assert(file_browser_name_is_hidden(".Trashes"));
    assert(file_browser_name_is_hidden(""));
    assert(file_browser_name_is_hidden(NULL));
    assert(!file_browser_name_is_hidden("track.mp3"));
}

static void test_add_and_filter(void)
{
    file_browser_entry_t storage[8];
    file_browser_dir_t dir;
    file_browser_dir_init(&dir, storage, 8U, "/usb0");
    assert(file_browser_dir_count(&dir) == 0U);
    assert(strcmp(dir.path, "/usb0") == 0);

    assert(file_browser_dir_add(&dir, "track.mp3", FILE_BROWSER_ENTRY_FILE));
    assert(file_browser_dir_add(&dir, "Album", FILE_BROWSER_ENTRY_DIRECTORY));
    /* Filtered out, and not counted as a loss: the user did not lose a track. */
    assert(!file_browser_dir_add(&dir, "cover.jpg", FILE_BROWSER_ENTRY_FILE));
    assert(!file_browser_dir_add(&dir, ".", FILE_BROWSER_ENTRY_DIRECTORY));
    assert(!file_browser_dir_add(&dir, "..", FILE_BROWSER_ENTRY_DIRECTORY));
    assert(!file_browser_dir_add(&dir, "._track.mp3", FILE_BROWSER_ENTRY_FILE));
    assert(file_browser_dir_count(&dir) == 2U);
    assert(dir.dropped_full == 0U && dir.dropped_long_name == 0U);

    const file_browser_entry_t *entry = file_browser_dir_entry(&dir, 0U);
    assert(entry != NULL && strcmp(entry->name, "track.mp3") == 0);
    assert(entry->kind == FILE_BROWSER_ENTRY_FILE);
    assert(entry->format == FILE_BROWSER_FORMAT_MP3);
    entry = file_browser_dir_entry(&dir, 1U);
    assert(entry != NULL && entry->kind == FILE_BROWSER_ENTRY_DIRECTORY);
    /* Directories carry no format even when the folder name ends in ".mp3". */
    assert(entry->format == FILE_BROWSER_FORMAT_NONE);
    assert(file_browser_dir_entry(&dir, 2U) == NULL);
    assert(file_browser_dir_entry(NULL, 0U) == NULL);
}

static void test_dropped_entries_are_counted(void)
{
    file_browser_entry_t storage[2];
    file_browser_dir_t dir;
    file_browser_dir_init(&dir, storage, 2U, "/usb0");
    assert(file_browser_dir_add(&dir, "a.mp3", FILE_BROWSER_ENTRY_FILE));
    assert(file_browser_dir_add(&dir, "b.mp3", FILE_BROWSER_ENTRY_FILE));
    assert(!file_browser_dir_add(&dir, "c.mp3", FILE_BROWSER_ENTRY_FILE));
    assert(dir.dropped_full == 1U);
    assert(file_browser_dir_count(&dir) == 2U);

    /* A name that does not fit is dropped rather than truncated: a truncated
     * name cannot be reopened as a path. */
    char long_name[FILE_BROWSER_NAME_MAX_LEN + 8];
    memset(long_name, 'x', sizeof(long_name) - 5U);
    memcpy(long_name + sizeof(long_name) - 5U, ".mp3", 5U);
    file_browser_dir_init(&dir, storage, 2U, "/usb0");
    assert(!file_browser_dir_add(&dir, long_name, FILE_BROWSER_ENTRY_FILE));
    assert(dir.dropped_long_name == 1U);
    assert(file_browser_dir_count(&dir) == 0U);

    /* Storage is mandatory; an unbacked listing must refuse, not crash. */
    file_browser_dir_init(&dir, NULL, 8U, "/usb0");
    assert(!file_browser_dir_add(&dir, "a.mp3", FILE_BROWSER_ENTRY_FILE));
    assert(file_browser_dir_count(&dir) == 0U);
}

static void test_sort_order(void)
{
    file_browser_entry_t storage[8];
    file_browser_dir_t dir;
    file_browser_dir_init(&dir, storage, 8U, "/usb0");
    assert(file_browser_dir_add(&dir, "zeta.mp3", FILE_BROWSER_ENTRY_FILE));
    assert(file_browser_dir_add(&dir, "Beta", FILE_BROWSER_ENTRY_DIRECTORY));
    assert(file_browser_dir_add(&dir, "alpha.mp3", FILE_BROWSER_ENTRY_FILE));
    assert(file_browser_dir_add(&dir, "arch", FILE_BROWSER_ENTRY_DIRECTORY));
    file_browser_dir_sort(&dir);

    /* Directories first, then case-insensitive by name. */
    assert(strcmp(file_browser_dir_entry(&dir, 0U)->name, "alpha.mp3") != 0);
    assert(strcmp(file_browser_dir_entry(&dir, 0U)->name, "arch") == 0);
    assert(strcmp(file_browser_dir_entry(&dir, 1U)->name, "Beta") == 0);
    assert(strcmp(file_browser_dir_entry(&dir, 2U)->name, "alpha.mp3") == 0);
    assert(strcmp(file_browser_dir_entry(&dir, 3U)->name, "zeta.mp3") == 0);

    file_browser_dir_sort(NULL);
}

static void test_next_file(void)
{
    file_browser_entry_t storage[8];
    file_browser_dir_t dir;
    file_browser_dir_init(&dir, storage, 8U, "/usb0");
    assert(file_browser_dir_add(&dir, "Album", FILE_BROWSER_ENTRY_DIRECTORY));
    assert(file_browser_dir_add(&dir, "Second", FILE_BROWSER_ENTRY_DIRECTORY));
    assert(file_browser_dir_add(&dir, "a.mp3", FILE_BROWSER_ENTRY_FILE));
    assert(file_browser_dir_add(&dir, "b.mp3", FILE_BROWSER_ENTRY_FILE));
    file_browser_dir_sort(&dir);

    /* Skips the leading directories, then walks the files and finally reports
     * "no more" as the entry count, which is what auto-advance stops on. */
    assert(file_browser_dir_next_file(&dir, 0U) == 2U);
    assert(file_browser_dir_next_file(&dir, 2U) == 2U);
    assert(file_browser_dir_next_file(&dir, 3U) == 3U);
    assert(file_browser_dir_next_file(&dir, 4U) == 4U);
    assert(file_browser_dir_next_file(&dir, 99U) == 4U);

    file_browser_dir_init(&dir, storage, 8U, "/usb0");
    assert(file_browser_dir_add(&dir, "Only", FILE_BROWSER_ENTRY_DIRECTORY));
    assert(file_browser_dir_next_file(&dir, 0U) == 1U);
}

static void test_previous_file(void)
{
    file_browser_entry_t storage[8];
    file_browser_dir_t dir;
    file_browser_dir_init(&dir, storage, 8U, "/usb0");
    assert(file_browser_dir_add(&dir, "Album", FILE_BROWSER_ENTRY_DIRECTORY));
    assert(file_browser_dir_add(&dir, "Second", FILE_BROWSER_ENTRY_DIRECTORY));
    assert(file_browser_dir_add(&dir, "a.mp3", FILE_BROWSER_ENTRY_FILE));
    assert(file_browser_dir_add(&dir, "b.mp3", FILE_BROWSER_ENTRY_FILE));
    file_browser_dir_sort(&dir);

    /* Strictly before the row asked about, so the file that is playing is
     * never handed back as its own predecessor. */
    assert(file_browser_dir_previous_file(&dir, 3U) == 2U);
    /* At the first file there is nothing before it but directories, and "none"
     * is the entry count - the same answer next_file gives at the other end,
     * so the two read alike. */
    assert(file_browser_dir_previous_file(&dir, 2U) == 4U);
    assert(file_browser_dir_previous_file(&dir, 0U) == 4U);
    /* A row past the end still answers about the last file there is. */
    assert(file_browser_dir_previous_file(&dir, 99U) == 3U);

    file_browser_dir_init(&dir, storage, 8U, "/usb0");
    assert(file_browser_dir_add(&dir, "Only", FILE_BROWSER_ENTRY_DIRECTORY));
    assert(file_browser_dir_previous_file(&dir, 1U) == 1U);
}

static void test_paths(void)
{
    char path[FILE_BROWSER_PATH_MAX_LEN];
    assert(file_browser_path_is_root("/usb0"));
    assert(!file_browser_path_is_root("/usb0/Album"));
    assert(!file_browser_path_is_root(NULL));
    // Every volume's root, not just the drive's: the card mounts at /sd0 and
    // browsing up out of it has to stop in exactly the same place.
    assert(file_browser_path_is_root("/sd0"));
    assert(!file_browser_path_is_root("/sd0/Music/1.mp3"));
    assert(!file_browser_path_is_root("/"));
    assert(!file_browser_path_is_root("usb0"));

    char root[64];
    assert(file_browser_path_volume_root("/sd0/Music/1.mp3", root, sizeof(root)));
    assert(strcmp(root, "/sd0") == 0);
    assert(file_browser_path_volume_root("/usb0", root, sizeof(root)));
    assert(strcmp(root, "/usb0") == 0);
    // Refused rather than truncated: "/usb" is a volume nobody has.
    assert(!file_browser_path_volume_root("/usb0/x", root, 5U));
    assert(!file_browser_path_volume_root("relative/path", root, sizeof(root)));
    assert(!file_browser_path_volume_root("/", root, sizeof(root)));

    assert(file_browser_path_child("/usb0", "Album", path, sizeof(path)));
    assert(strcmp(path, "/usb0/Album") == 0);

    char child[FILE_BROWSER_PATH_MAX_LEN];
    assert(file_browser_path_child(path, "track.mp3", child, sizeof(child)));
    assert(strcmp(child, "/usb0/Album/track.mp3") == 0);
    assert(file_browser_path_child("/usb0/Album", "Танцуем.mp3", child, sizeof(child)));
    assert(strcmp(child, "/usb0/Album/Танцуем.mp3") == 0);

    /* Escaping the mount point is refused rather than sanitised. */
    assert(!file_browser_path_child("/usb0", "..", child, sizeof(child)));
    assert(!file_browser_path_child("/usb0", "sub/dir", child, sizeof(child)));
    assert(!file_browser_path_child(NULL, "a", child, sizeof(child)));

    /* Too long to fit is a refusal, never a truncation. "/usb0/Album" needs
     * exactly 12 bytes with its terminator, so 12 fits and 11 must not. */
    char exact[12];
    assert(file_browser_path_child("/usb0", "Album", exact, sizeof(exact)));
    assert(strcmp(exact, "/usb0/Album") == 0);
    char small[11];
    assert(!file_browser_path_child("/usb0", "Album", small, sizeof(small)));

    assert(file_browser_path_parent("/usb0/Album/Deep", child, sizeof(child)));
    assert(strcmp(child, "/usb0/Album") == 0);
    assert(file_browser_path_parent("/usb0/Album", child, sizeof(child)));
    assert(strcmp(child, "/usb0") == 0);
    /* The root has no parent: there is nothing above the mount point. */
    assert(!file_browser_path_parent("/usb0", child, sizeof(child)));
    assert(!file_browser_path_parent(NULL, child, sizeof(child)));
}


static void test_finding_an_entry_by_name(void)
{
    /* Only the name survives a restart, so the remembered track is located
     * this way rather than by the index it used to have. */
    file_browser_entry_t storage[8];
    file_browser_dir_t dir;
    file_browser_dir_init(&dir, storage, 8U, "/usb0");
    assert(file_browser_dir_add(&dir, "b.mp3", FILE_BROWSER_ENTRY_FILE));
    assert(file_browser_dir_add(&dir, "Album", FILE_BROWSER_ENTRY_DIRECTORY));
    assert(file_browser_dir_add(&dir, "a.mp3", FILE_BROWSER_ENTRY_FILE));
    file_browser_dir_sort(&dir);

    const size_t found = file_browser_dir_find(&dir, "a.mp3");
    assert(found < file_browser_dir_count(&dir));
    assert(strcmp(file_browser_dir_entry(&dir, found)->name, "a.mp3") == 0);
    /* Directories are found too - the caller decides what to do with one. */
    assert(file_browser_dir_find(&dir, "Album") < file_browser_dir_count(&dir));

    /* A gone track reports past the end, which is how the caller falls back to
     * the browser rather than playing the wrong file. */
    assert(file_browser_dir_find(&dir, "gone.mp3") == file_browser_dir_count(&dir));
    assert(file_browser_dir_find(&dir, "") == file_browser_dir_count(&dir));
    assert(file_browser_dir_find(&dir, NULL) == file_browser_dir_count(&dir));
    assert(file_browser_dir_find(NULL, "a.mp3") == 0U);
}

/* A playlist in a directory is listed as its own kind, so that everything
 * walking the listing knows the click opens something rather than plays it. */
static void test_playlists_in_a_directory(void)
{
    file_browser_entry_t storage[8];
    file_browser_dir_t dir;
    file_browser_dir_init(&dir, storage, 8U, "/usb0");

    assert(file_browser_dir_add(&dir, "z.mp3", FILE_BROWSER_ENTRY_FILE));
    assert(file_browser_dir_add(&dir, "playlist.m3u", FILE_BROWSER_ENTRY_FILE));
    assert(file_browser_dir_add(&dir, "radio.PLS", FILE_BROWSER_ENTRY_FILE));
    assert(file_browser_dir_add(&dir, "Music", FILE_BROWSER_ENTRY_DIRECTORY));
    // Still nothing playable about a text file beside the music.
    assert(!file_browser_dir_add(&dir, "notes.txt", FILE_BROWSER_ENTRY_FILE));
    file_browser_dir_sort(&dir);

    // Directories, then playlists, then tracks: what opens sits above what
    // plays, and one playlist is not buried under a hundred files.
    assert(file_browser_dir_entry(&dir, 0U)->kind == FILE_BROWSER_ENTRY_DIRECTORY);
    assert(file_browser_dir_entry(&dir, 1U)->kind == FILE_BROWSER_ENTRY_PLAYLIST);
    assert(strcmp(file_browser_dir_entry(&dir, 1U)->name, "playlist.m3u") == 0);
    assert(file_browser_dir_entry(&dir, 2U)->kind == FILE_BROWSER_ENTRY_PLAYLIST);
    assert(file_browser_dir_entry(&dir, 3U)->kind == FILE_BROWSER_ENTRY_FILE);

    // The type column names the list format, and nothing for a directory.
    assert(strcmp(file_browser_entry_type_label(file_browser_dir_entry(&dir, 0U)), "") == 0);
    assert(strcmp(file_browser_entry_type_label(file_browser_dir_entry(&dir, 1U)), "M3U") == 0);
    assert(strcmp(file_browser_entry_type_label(file_browser_dir_entry(&dir, 2U)), "PLS") == 0);
    assert(strcmp(file_browser_entry_type_label(file_browser_dir_entry(&dir, 3U)), "MP3") == 0);
    assert(strcmp(file_browser_entry_type_label(NULL), "") == 0);

    /* Track advance steps over a playlist the way it steps over a directory:
     * the only entry it may land on is one a decoder can open. */
    assert(file_browser_dir_next_file(&dir, 0U) == 3U);
    assert(file_browser_dir_previous_file(&dir, 3U) == file_browser_dir_count(&dir));
}

/* A .cue sheet lists tracks as places in files: the row carries which track
 * and where, several rows may share a file, and a track in a format nothing
 * here plays is counted rather than listed. */
static void test_cue_tracks_are_rows_with_a_place_in_their_file(void)
{
    file_browser_entry_t storage[4];
    file_browser_dir_t dir;
    file_browser_dir_init_playlist(&dir, storage, 4U, "/usb0/Music/LP/album.cue");
    assert(playlist_file_kind_from_name("album.CUE") == PLAYLIST_FILE_CUE);
    assert(strcmp(playlist_file_kind_name(PLAYLIST_FILE_CUE), "CUE") == 0);
    assert(file_browser_dir_add_cue_track(&dir, "01. Side 1.flac", 1U, 0U, 12700U));
    assert(file_browser_dir_add_cue_track(&dir, "01. Side 1.flac", 2U, 12700U, 0U));
    assert(!file_browser_dir_add_cue_track(&dir, "side 2.ape", 3U, 0U, 0U));
    assert(dir.count == 2U && dir.dropped_unplayable == 1U);
    assert(storage[1].cue_track == 2U && storage[1].cue_start_frames == 12700U);
    assert(storage[1].cue_end_frames == 0U && storage[1].format == FILE_BROWSER_FORMAT_FLAC);
    char path[FILE_BROWSER_PATH_MAX_LEN];
    assert(file_browser_dir_path_for(&dir, storage[0].name, path, sizeof(path)));
    assert(strcmp(path, "/usb0/Music/LP/01. Side 1.flac") == 0);
    // An ordinary row carries no cue place.
    file_browser_dir_init(&dir, storage, 4U, "/usb0");
    assert(file_browser_dir_add(&dir, "a.mp3", FILE_BROWSER_ENTRY_FILE));
    assert(storage[0].cue_track == 0U && storage[0].cue_start_frames == 0U);
}

static cue_sheet_t s_sheet;

static void add_sheet_track(uint8_t number, uint8_t file, uint32_t start, const char *title)
{
    cue_sheet_track_t *track = &s_sheet.tracks[s_sheet.track_count++];
    track->number = number;
    track->file = file;
    track->start_frames = start;
    snprintf(track->title, sizeof(track->title), "%s", title);
}

/* In a folder, a sheet's tracks stand in for the sheet and the files it cuts
 * up: the folder reads like a disc ripped to tracks. A FILE the sheet wrote as
 * .wav is found as the .flac it was packed into, and loose tracks stay. */
static void test_a_sheet_in_a_folder_becomes_its_tracks(void)
{
    file_browser_entry_t storage[8];
    file_browser_dir_t dir;
    file_browser_dir_init(&dir, storage, 8U, "/usb0/LP");
    assert(file_browser_dir_add(&dir, "Side A.flac", FILE_BROWSER_ENTRY_FILE));
    assert(file_browser_dir_add(&dir, "side b.flac", FILE_BROWSER_ENTRY_FILE));
    assert(file_browser_dir_add(&dir, "bonus.mp3", FILE_BROWSER_ENTRY_FILE));
    assert(file_browser_dir_add(&dir, "album.cue", FILE_BROWSER_ENTRY_FILE));
    assert(file_browser_dir_add(&dir, "Scans", FILE_BROWSER_ENTRY_DIRECTORY));
    file_browser_dir_sort(&dir);
    assert(storage[1].kind == FILE_BROWSER_ENTRY_PLAYLIST);

    memset(&s_sheet, 0, sizeof(s_sheet));
    snprintf(s_sheet.files[0], sizeof(s_sheet.files[0]), "side a.flac");
    snprintf(s_sheet.files[1], sizeof(s_sheet.files[1]), "Side B.wav");
    snprintf(s_sheet.files[2], sizeof(s_sheet.files[2]), "Side C.flac");
    s_sheet.file_count = 3U;
    add_sheet_track(1U, 0U, 0U, "One");
    add_sheet_track(2U, 0U, 12700U, "Two");
    add_sheet_track(3U, 1U, 0U, "Three");
    add_sheet_track(4U, 2U, 0U, "Missing");

    assert(file_browser_dir_expand_cue(&dir, 1U, &s_sheet, 1U) == 3U);
    assert(dir.count == 5U && dir.dropped_unplayable == 1U);
    assert(strcmp(storage[0].name, "Scans") == 0);
    assert(strcmp(storage[1].name, "bonus.mp3") == 0 && storage[1].cue_track == 0U);
    assert(strcmp(storage[2].name, "Side A.flac") == 0 && storage[2].cue_track == 1U);
    assert(storage[2].cue_sheet == 1U && storage[2].cue_end_frames == 12700U);
    assert(storage[3].cue_track == 2U && storage[3].cue_start_frames == 12700U);
    assert(storage[3].cue_end_frames == 0U);
    // The row names the file the folder has, in the format it really is.
    assert(strcmp(storage[4].name, "side b.flac") == 0 && storage[4].cue_track == 3U);
    assert(storage[4].format == FILE_BROWSER_FORMAT_FLAC);
    char path[FILE_BROWSER_PATH_MAX_LEN];
    assert(file_browser_dir_path_for(&dir, storage[4].name, path, sizeof(path)));
    assert(strcmp(path, "/usb0/LP/side b.flac") == 0);
    assert(file_browser_dir_next_file(&dir, 3U) == 3U);

    // A sheet whose files are not here keeps its row and changes nothing.
    file_browser_dir_init(&dir, storage, 8U, "/usb0/LP");
    assert(file_browser_dir_add(&dir, "other.cue", FILE_BROWSER_ENTRY_FILE));
    assert(file_browser_dir_add(&dir, "bonus.mp3", FILE_BROWSER_ENTRY_FILE));
    assert(file_browser_dir_expand_cue(&dir, 0U, &s_sheet, 1U) == 0U);
    assert(dir.count == 2U && dir.dropped_unplayable == 0U);
    assert(storage[0].kind == FILE_BROWSER_ENTRY_PLAYLIST);
}

int main(void)
{
    test_a_sheet_in_a_folder_becomes_its_tracks();
    test_cue_tracks_are_rows_with_a_place_in_their_file();
    test_format_from_name();
    test_hidden_names();
    test_add_and_filter();
    test_dropped_entries_are_counted();
    test_sort_order();
    test_next_file();
    test_previous_file();
    test_paths();
    test_finding_an_entry_by_name();
    test_playlists_in_a_directory();
    printf("file_browser tests passed\n");
    return 0;
}
