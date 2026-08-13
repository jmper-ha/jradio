#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USB_BROWSER_ROOT_PATH "/usb0"

// FATFS is configured with MAX_LFN=255 and UTF-8 API encoding, so a Cyrillic
// name can reach ~510 bytes. Names are stored whole and never truncated: a
// truncated name still has to open as a path, and a half-name silently opens
// nothing. Entries that do not fit are dropped and counted instead.
#define USB_BROWSER_NAME_MAX_LEN 256
#define USB_BROWSER_PATH_MAX_LEN 1024

/* Why three states rather than a bool: "no drive" and "a drive that will not
 * read" need different words on screen, and telling a user to insert a stick
 * that is already inserted is worse than saying nothing. */
typedef enum {
    USB_BROWSER_MEDIA_ABSENT = 0,
    USB_BROWSER_MEDIA_READY,
    USB_BROWSER_MEDIA_UNREADABLE,
} usb_browser_media_t;

typedef enum {
    USB_BROWSER_ENTRY_DIRECTORY = 0,
    USB_BROWSER_ENTRY_FILE,
} usb_browser_entry_kind_t;

typedef enum {
    USB_BROWSER_FORMAT_NONE = 0,
    USB_BROWSER_FORMAT_MP3,
    USB_BROWSER_FORMAT_AAC,
    USB_BROWSER_FORMAT_FLAC,
    USB_BROWSER_FORMAT_OGG_FLAC,
    USB_BROWSER_FORMAT_WAV,
} usb_browser_format_t;

typedef struct {
    char name[USB_BROWSER_NAME_MAX_LEN];
    usb_browser_entry_kind_t kind;
    // USB_BROWSER_FORMAT_NONE for directories.
    usb_browser_format_t format;
} usb_browser_entry_t;

typedef struct {
    // Caller-owned storage: on the device this is a PSRAM block, in tests a
    // plain array. Keeping it out of the struct lets the capacity change
    // without touching this header.
    usb_browser_entry_t *entries;
    size_t capacity;
    size_t count;
    // Entries the directory really had but this listing dropped, split by
    // reason so the UI can say which one happened.
    size_t dropped_full;
    size_t dropped_long_name;
    char path[USB_BROWSER_PATH_MAX_LEN];
} usb_browser_dir_t;

// Classifies by file-name extension. Deliberately narrow: only the containers
// radio_decoder already decodes, plus WAV.
//
// ".m4a" is excluded on purpose - it is AAC in an MP4 container, and the AAC
// backend consumes raw ADTS frames, so accepting it would list files that
// always fail at play time. ".ogg"/".oga" map to Ogg FLAC; Vorbis inside Ogg
// is listed but will fail in the decoder, which is still better than hiding
// the file with no explanation.
usb_browser_format_t usb_browser_format_from_name(const char *name);
const char *usb_browser_format_name(usb_browser_format_t format);

// True for "." / ".." and for dot-files. Media players that wrote to the drive
// leave "._track.mp3" AppleDouble stubs and ".Trashes" behind; listing those
// as playable tracks is noise.
bool usb_browser_name_is_hidden(const char *name);

void usb_browser_dir_init(usb_browser_dir_t *dir, usb_browser_entry_t *storage,
                          size_t capacity, const char *path);

// Returns true when the entry was stored. Filtered-out entries (hidden names,
// files in no supported format) return false without touching the counters:
// they are not something the user lost.
bool usb_browser_dir_add(usb_browser_dir_t *dir, const char *name,
                         usb_browser_entry_kind_t kind);

// Directories first, then names, case-insensitive for ASCII. Non-ASCII bytes
// compare bytewise, which orders UTF-8 Cyrillic self-consistently (though not
// by the Russian alphabet) - full Unicode collation is not worth the flash.
void usb_browser_dir_sort(usb_browser_dir_t *dir);

size_t usb_browser_dir_count(const usb_browser_dir_t *dir);
const usb_browser_entry_t *usb_browser_dir_entry(const usb_browser_dir_t *dir, size_t index);

// Index of the next playable file at or after `from`, or the entry count when
// the directory has none left. Drives both "play what the cursor is on" and
// auto-advance to the next track.
size_t usb_browser_dir_next_file(const usb_browser_dir_t *dir, size_t from);

bool usb_browser_path_is_root(const char *path);
// Both return false rather than truncating: a truncated path is a path to the
// wrong file, and silently opening the wrong file is worse than refusing.
bool usb_browser_path_child(const char *path, const char *name, char *out, size_t out_size);
bool usb_browser_path_parent(const char *path, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
