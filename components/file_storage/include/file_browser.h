#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "playlist_file.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Where each volume is mounted.
 *
 * These live in the pure layer, not with the drivers that mount them, because
 * a path is the only thing that says which volume something is on - and the
 * resume point, which is nothing but a path, has to be mapped back to a source
 * without any of the mounting code in reach. usb_storage.h and sd_storage.h
 * re-export them under their own names. */
#define FILE_BROWSER_USB_ROOT "/usb0"
#define FILE_BROWSER_SD_ROOT "/sd0"

// FATFS is configured with MAX_LFN=255 and UTF-8 API encoding, so a Cyrillic
// name can reach ~510 bytes. Names are stored whole and never truncated: a
// truncated name still has to open as a path, and a half-name silently opens
// nothing. Entries that do not fit are dropped and counted instead.
#define FILE_BROWSER_NAME_MAX_LEN 256
#define FILE_BROWSER_PATH_MAX_LEN 1024

/* Why three states rather than a bool: "no drive" and "a drive that will not
 * read" need different words on screen, and telling a user to insert a stick
 * that is already inserted is worse than saying nothing. */
typedef enum {
    FILE_BROWSER_MEDIA_ABSENT = 0,
    FILE_BROWSER_MEDIA_READY,
    FILE_BROWSER_MEDIA_UNREADABLE,
} file_browser_media_t;

typedef enum {
    FILE_BROWSER_ENTRY_DIRECTORY = 0,
    FILE_BROWSER_ENTRY_FILE,
    /* An .m3u or .pls file. Its own kind rather than a file with an odd
     * format, because everything that walks a listing has to treat it as a
     * container: selecting it opens what it names, and track advance steps
     * over it the way it steps over a directory. */
    FILE_BROWSER_ENTRY_PLAYLIST,
} file_browser_entry_kind_t;

typedef enum {
    FILE_BROWSER_FORMAT_NONE = 0,
    FILE_BROWSER_FORMAT_MP3,
    FILE_BROWSER_FORMAT_AAC,
    FILE_BROWSER_FORMAT_FLAC,
    FILE_BROWSER_FORMAT_OGG_FLAC,
    FILE_BROWSER_FORMAT_WAV,
} file_browser_format_t;

/* What the open listing is: the contents of a directory, or the tracks a
 * playlist file names.
 *
 * The difference is in what an entry name means. In a directory it is a name
 * inside dir->path. In a playlist it is the reference as the file wrote it -
 * a path relative to the directory the *playlist* is in, which may lead
 * through folders of its own ("Music/Album/1.mp3") or be a full path on the
 * volume. file_browser_dir_path_for() is the one place that knows this. */
typedef enum {
    FILE_BROWSER_LISTING_DIRECTORY = 0,
    FILE_BROWSER_LISTING_PLAYLIST,
} file_browser_listing_t;

typedef struct {
    // In a playlist listing this holds the reference, slashes and all, so it
    // can be longer than the file name a browser row shows - see
    // file_browser_entry_display_name().
    char name[FILE_BROWSER_NAME_MAX_LEN];
    file_browser_entry_kind_t kind;
    // FILE_BROWSER_FORMAT_NONE for directories.
    file_browser_format_t format;
} file_browser_entry_t;

typedef struct {
    // Caller-owned storage: on the device this is a PSRAM block, in tests a
    // plain array. Keeping it out of the struct lets the capacity change
    // without touching this header.
    file_browser_entry_t *entries;
    size_t capacity;
    size_t count;
    // Entries the directory really had but this listing dropped, split by
    // reason so the UI can say which one happened.
    size_t dropped_full;
    size_t dropped_long_name;
    /* Tracks the playlist named but this device cannot open: streams, drive
     * letters, formats with no decoder. Counted rather than dropped silently,
     * so a short list has an explanation. */
    size_t dropped_unplayable;
    file_browser_listing_t listing;
    // The directory being shown, or - for a playlist listing - the path of the
    // playlist file itself.
    char path[FILE_BROWSER_PATH_MAX_LEN];
} file_browser_dir_t;

// Classifies by file-name extension. Deliberately narrow: only the containers
// radio_decoder already decodes, plus WAV.
//
// ".m4a" is excluded on purpose - it is AAC in an MP4 container, and the AAC
// backend consumes raw ADTS frames, so accepting it would list files that
// always fail at play time. ".ogg"/".oga" map to Ogg FLAC; Vorbis inside Ogg
// is listed but will fail in the decoder, which is still better than hiding
// the file with no explanation.
file_browser_format_t file_browser_format_from_name(const char *name);
const char *file_browser_format_name(file_browser_format_t format);

// What the type column shows for one entry: the audio format for a track,
// "M3U"/"PLS" for a playlist, nothing for a directory.
const char *file_browser_entry_type_label(const file_browser_entry_t *entry);

/* The part of an entry name a browser row shows, and the name a playing track
 * is announced under.
 *
 * Identical to the name itself everywhere except inside a playlist, where the
 * name is the reference the file wrote and can carry the folders it leads
 * through. A 116 px row would then show the start of a path and cut off the
 * track. Nothing is copied: the pointer is into the name passed in. */
const char *file_browser_display_name(const char *name);

// True for "." / ".." and for dot-files. Media players that wrote to the drive
// leave "._track.mp3" AppleDouble stubs and ".Trashes" behind; listing those
// as playable tracks is noise.
bool file_browser_name_is_hidden(const char *name);

void file_browser_dir_init(file_browser_dir_t *dir, file_browser_entry_t *storage,
                          size_t capacity, const char *path);

// The same, for the tracks of the playlist file at `path`. Entries added
// afterwards are references rather than names, and the listing must not be
// sorted: the order the file writes them in is the order they play in.
void file_browser_dir_init_playlist(file_browser_dir_t *dir, file_browser_entry_t *storage,
                                   size_t capacity, const char *path);

// Returns true when the entry was stored. Filtered-out entries (hidden names,
// files in no supported format) return false without touching the counters:
// they are not something the user lost.
bool file_browser_dir_add(file_browser_dir_t *dir, const char *name,
                         file_browser_entry_kind_t kind);

// Directories first, then names, case-insensitive for ASCII. Non-ASCII bytes
// compare bytewise, which orders UTF-8 Cyrillic self-consistently (though not
// by the Russian alphabet) - full Unicode collation is not worth the flash.
void file_browser_dir_sort(file_browser_dir_t *dir);

size_t file_browser_dir_count(const file_browser_dir_t *dir);
const file_browser_entry_t *file_browser_dir_entry(const file_browser_dir_t *dir, size_t index);

// Index of the next playable file at or after `from`, or the entry count when
// the directory has none left. Drives both "play what the cursor is on" and
// auto-advance to the next track.
size_t file_browser_dir_next_file(const file_browser_dir_t *dir, size_t from);
/* Index of the last playable file *before* `before`, or the entry count when
 * there is none - the same "past the end means nothing left" answer next_file
 * gives, so the two read alike at the two ends of a directory. */
size_t file_browser_dir_previous_file(const file_browser_dir_t *dir, size_t before);

/* Index of the entry called `name`, or the entry count when there is none.
 * Used to find a remembered track again after a restart, where only the name
 * survived. */
size_t file_browser_dir_find(const file_browser_dir_t *dir, const char *name);

bool file_browser_path_is_root(const char *path);

/* Copies the volume root out of a path: "/sd0/Music/1.mp3" gives "/sd0", and a
 * path that is already a root gives itself back.
 *
 * What it is for: a remembered path is the only thing that says which volume a
 * resume point lives on, and when the directory it named is gone the browser
 * still has to open somewhere on that same volume. False rather than a
 * truncated root - a truncated root names a volume that does not exist. */
bool file_browser_path_volume_root(const char *path, char *out, size_t out_size);

// True when `path` names something on `root` - "/sd0" matches "/sd0" and
// "/sd0/Music/1.mp3", and never "/sd00".
bool file_browser_path_on_volume(const char *path, const char *root);
// Both return false rather than truncating: a truncated path is a path to the
// wrong file, and silently opening the wrong file is worse than refusing.
bool file_browser_path_child(const char *path, const char *name, char *out, size_t out_size);

/* The full path an entry of `dir` opens, whichever kind of listing it is.
 *
 * For a directory this is the directory plus the name. For a playlist it is
 * the reference resolved against the directory the playlist file sits in -
 * which is what makes both playlists on the test drive work: the .m3u in the
 * root writes "Music/Album/1.mp3", and the .pls inside Music/ writes
 * "Album/1.mp3", and each is relative to its own file.
 *
 * Refuses, rather than truncating, for the reason path_child does; refuses
 * an unsafe reference for the reason playlist_file_reference_is_safe() gives. */
bool file_browser_dir_path_for(const file_browser_dir_t *dir, const char *name, char *out,
                              size_t out_size);
bool file_browser_path_parent(const char *path, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
