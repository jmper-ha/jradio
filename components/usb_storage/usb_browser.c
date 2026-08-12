#include "usb_browser.h"

#include <stdlib.h>
#include <string.h>

static char ascii_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static int compare_names(const char *left, const char *right)
{
    for (size_t i = 0U;; ++i) {
        const unsigned char a = (unsigned char)ascii_lower(left[i]);
        const unsigned char b = (unsigned char)ascii_lower(right[i]);
        if (a != b) return a < b ? -1 : 1;
        if (a == '\0') return 0;
    }
}

static const char *extension_of(const char *name)
{
    const char *dot = NULL;
    for (const char *c = name; *c != '\0'; ++c) {
        if (*c == '.') dot = c;
    }
    // A leading dot is the hidden-file marker, not an extension separator.
    return (dot == NULL || dot == name) ? NULL : dot + 1;
}

static bool extension_equals(const char *extension, const char *expected)
{
    return compare_names(extension, expected) == 0;
}

usb_browser_format_t usb_browser_format_from_name(const char *name)
{
    if (name == NULL) return USB_BROWSER_FORMAT_NONE;
    const char *extension = extension_of(name);
    if (extension == NULL) return USB_BROWSER_FORMAT_NONE;
    if (extension_equals(extension, "mp3")) return USB_BROWSER_FORMAT_MP3;
    if (extension_equals(extension, "aac")) return USB_BROWSER_FORMAT_AAC;
    if (extension_equals(extension, "adts")) return USB_BROWSER_FORMAT_AAC;
    if (extension_equals(extension, "flac")) return USB_BROWSER_FORMAT_FLAC;
    if (extension_equals(extension, "ogg")) return USB_BROWSER_FORMAT_OGG_FLAC;
    if (extension_equals(extension, "oga")) return USB_BROWSER_FORMAT_OGG_FLAC;
    if (extension_equals(extension, "wav")) return USB_BROWSER_FORMAT_WAV;
    return USB_BROWSER_FORMAT_NONE;
}

const char *usb_browser_format_name(usb_browser_format_t format)
{
    switch (format) {
    case USB_BROWSER_FORMAT_MP3: return "MP3";
    case USB_BROWSER_FORMAT_AAC: return "AAC";
    case USB_BROWSER_FORMAT_FLAC: return "FLAC";
    case USB_BROWSER_FORMAT_OGG_FLAC: return "OGG";
    case USB_BROWSER_FORMAT_WAV: return "WAV";
    case USB_BROWSER_FORMAT_NONE:
    default: return "";
    }
}

bool usb_browser_name_is_hidden(const char *name)
{
    return name == NULL || name[0] == '\0' || name[0] == '.';
}

void usb_browser_dir_init(usb_browser_dir_t *dir, usb_browser_entry_t *storage,
                          size_t capacity, const char *path)
{
    if (dir == NULL) return;
    dir->entries = storage;
    dir->capacity = storage != NULL ? capacity : 0U;
    dir->count = 0U;
    dir->dropped_full = 0U;
    dir->dropped_long_name = 0U;
    dir->path[0] = '\0';
    if (path != NULL && strlen(path) < sizeof(dir->path)) {
        // strcpy is safe here precisely because of the length test above; an
        // over-long mount path is a programming error, not user data.
        strcpy(dir->path, path);
    }
}

bool usb_browser_dir_add(usb_browser_dir_t *dir, const char *name,
                         usb_browser_entry_kind_t kind)
{
    if (dir == NULL || dir->entries == NULL || usb_browser_name_is_hidden(name)) {
        return false;
    }
    usb_browser_format_t format = USB_BROWSER_FORMAT_NONE;
    if (kind == USB_BROWSER_ENTRY_FILE) {
        format = usb_browser_format_from_name(name);
        if (format == USB_BROWSER_FORMAT_NONE) return false;
    }
    const size_t length = strlen(name);
    if (length >= USB_BROWSER_NAME_MAX_LEN) {
        ++dir->dropped_long_name;
        return false;
    }
    if (dir->count >= dir->capacity) {
        ++dir->dropped_full;
        return false;
    }
    usb_browser_entry_t *entry = &dir->entries[dir->count];
    memcpy(entry->name, name, length + 1U);
    entry->kind = kind;
    entry->format = format;
    ++dir->count;
    return true;
}

static int compare_entries(const void *left, const void *right)
{
    const usb_browser_entry_t *a = (const usb_browser_entry_t *)left;
    const usb_browser_entry_t *b = (const usb_browser_entry_t *)right;
    if (a->kind != b->kind) {
        return a->kind == USB_BROWSER_ENTRY_DIRECTORY ? -1 : 1;
    }
    const int by_name = compare_names(a->name, b->name);
    // Case-insensitive comparison makes "Track.mp3" and "track.mp3" equal, and
    // qsort is not stable, so fall back to a bytewise compare to keep the order
    // reproducible across listings of the same directory.
    return by_name != 0 ? by_name : strcmp(a->name, b->name);
}

void usb_browser_dir_sort(usb_browser_dir_t *dir)
{
    if (dir == NULL || dir->entries == NULL || dir->count < 2U) return;
    qsort(dir->entries, dir->count, sizeof(dir->entries[0]), compare_entries);
}

size_t usb_browser_dir_count(const usb_browser_dir_t *dir)
{
    return dir == NULL ? 0U : dir->count;
}

const usb_browser_entry_t *usb_browser_dir_entry(const usb_browser_dir_t *dir, size_t index)
{
    if (dir == NULL || dir->entries == NULL || index >= dir->count) return NULL;
    return &dir->entries[index];
}

size_t usb_browser_dir_next_file(const usb_browser_dir_t *dir, size_t from)
{
    if (dir == NULL) return 0U;
    for (size_t i = from; i < dir->count; ++i) {
        if (dir->entries[i].kind == USB_BROWSER_ENTRY_FILE) return i;
    }
    return dir->count;
}

bool usb_browser_path_is_root(const char *path)
{
    return path != NULL && strcmp(path, USB_BROWSER_ROOT_PATH) == 0;
}

bool usb_browser_path_child(const char *path, const char *name, char *out, size_t out_size)
{
    if (path == NULL || name == NULL || out == NULL || out_size == 0U) return false;
    // "." and ".." would walk out of the mount point; nothing above /usb0 is
    // ours to browse.
    if (usb_browser_name_is_hidden(name) || strchr(name, '/') != NULL) return false;
    const size_t path_length = strlen(path);
    const size_t name_length = strlen(name);
    if (path_length + 1U + name_length + 1U > out_size) return false;
    memcpy(out, path, path_length);
    out[path_length] = '/';
    memcpy(out + path_length + 1U, name, name_length + 1U);
    return true;
}

bool usb_browser_path_parent(const char *path, char *out, size_t out_size)
{
    if (path == NULL || out == NULL || out_size == 0U || usb_browser_path_is_root(path)) {
        return false;
    }
    const char *slash = strrchr(path, '/');
    if (slash == NULL || slash == path) return false;
    const size_t length = (size_t)(slash - path);
    if (length + 1U > out_size) return false;
    memcpy(out, path, length);
    out[length] = '\0';
    return true;
}
