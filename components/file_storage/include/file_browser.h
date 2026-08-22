#pragma once

#include <stdbool.h>
#include <stddef.h>

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
} file_browser_entry_kind_t;

typedef enum {
    FILE_BROWSER_FORMAT_NONE = 0,
    FILE_BROWSER_FORMAT_MP3,
    FILE_BROWSER_FORMAT_AAC,
    FILE_BROWSER_FORMAT_FLAC,
    FILE_BROWSER_FORMAT_OGG_FLAC,
    FILE_BROWSER_FORMAT_WAV,
} file_browser_format_t;

typedef struct {
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

// True for "." / ".." and for dot-files. Media players that wrote to the drive
// leave "._track.mp3" AppleDouble stubs and ".Trashes" behind; listing those
// as playable tracks is noise.
bool file_browser_name_is_hidden(const char *name);

void file_browser_dir_init(file_browser_dir_t *dir, file_browser_entry_t *storage,
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
bool file_browser_path_parent(const char *path, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
