#include "file_browser.h"

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

file_browser_format_t file_browser_format_from_name(const char *name)
{
    if (name == NULL) return FILE_BROWSER_FORMAT_NONE;
    const char *extension = extension_of(name);
    if (extension == NULL) return FILE_BROWSER_FORMAT_NONE;
    if (extension_equals(extension, "mp3")) return FILE_BROWSER_FORMAT_MP3;
    if (extension_equals(extension, "aac")) return FILE_BROWSER_FORMAT_AAC;
    if (extension_equals(extension, "adts")) return FILE_BROWSER_FORMAT_AAC;
    if (extension_equals(extension, "flac")) return FILE_BROWSER_FORMAT_FLAC;
    if (extension_equals(extension, "ogg")) return FILE_BROWSER_FORMAT_OGG_FLAC;
    if (extension_equals(extension, "oga")) return FILE_BROWSER_FORMAT_OGG_FLAC;
    if (extension_equals(extension, "wav")) return FILE_BROWSER_FORMAT_WAV;
    return FILE_BROWSER_FORMAT_NONE;
}

const char *file_browser_format_name(file_browser_format_t format)
{
    switch (format) {
    case FILE_BROWSER_FORMAT_MP3: return "MP3";
    case FILE_BROWSER_FORMAT_AAC: return "AAC";
    case FILE_BROWSER_FORMAT_FLAC: return "FLAC";
    case FILE_BROWSER_FORMAT_OGG_FLAC: return "OGG";
    case FILE_BROWSER_FORMAT_WAV: return "WAV";
    case FILE_BROWSER_FORMAT_NONE:
    default: return "";
    }
}

bool file_browser_name_is_hidden(const char *name)
{
    return name == NULL || name[0] == '\0' || name[0] == '.';
}

void file_browser_dir_init(file_browser_dir_t *dir, file_browser_entry_t *storage,
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

bool file_browser_dir_add(file_browser_dir_t *dir, const char *name,
                         file_browser_entry_kind_t kind)
{
    if (dir == NULL || dir->entries == NULL || file_browser_name_is_hidden(name)) {
        return false;
    }
    file_browser_format_t format = FILE_BROWSER_FORMAT_NONE;
    if (kind == FILE_BROWSER_ENTRY_FILE) {
        format = file_browser_format_from_name(name);
        if (format == FILE_BROWSER_FORMAT_NONE) return false;
    }
    const size_t length = strlen(name);
    if (length >= FILE_BROWSER_NAME_MAX_LEN) {
        ++dir->dropped_long_name;
        return false;
    }
    if (dir->count >= dir->capacity) {
        ++dir->dropped_full;
        return false;
    }
    file_browser_entry_t *entry = &dir->entries[dir->count];
    memcpy(entry->name, name, length + 1U);
    entry->kind = kind;
    entry->format = format;
    ++dir->count;
    return true;
}

static int compare_entries(const void *left, const void *right)
{
    const file_browser_entry_t *a = (const file_browser_entry_t *)left;
    const file_browser_entry_t *b = (const file_browser_entry_t *)right;
    if (a->kind != b->kind) {
        return a->kind == FILE_BROWSER_ENTRY_DIRECTORY ? -1 : 1;
    }
    const int by_name = compare_names(a->name, b->name);
    // Case-insensitive comparison makes "Track.mp3" and "track.mp3" equal, and
    // qsort is not stable, so fall back to a bytewise compare to keep the order
    // reproducible across listings of the same directory.
    return by_name != 0 ? by_name : strcmp(a->name, b->name);
}

void file_browser_dir_sort(file_browser_dir_t *dir)
{
    if (dir == NULL || dir->entries == NULL || dir->count < 2U) return;
    qsort(dir->entries, dir->count, sizeof(dir->entries[0]), compare_entries);
}

size_t file_browser_dir_count(const file_browser_dir_t *dir)
{
    return dir == NULL ? 0U : dir->count;
}

const file_browser_entry_t *file_browser_dir_entry(const file_browser_dir_t *dir, size_t index)
{
    if (dir == NULL || dir->entries == NULL || index >= dir->count) return NULL;
    return &dir->entries[index];
}

size_t file_browser_dir_find(const file_browser_dir_t *dir, const char *name)
{
    const size_t count = file_browser_dir_count(dir);
    if (name == NULL || name[0] == '\0') return count;
    for (size_t index = 0U; index < count; ++index) {
        const file_browser_entry_t *entry = file_browser_dir_entry(dir, index);
        /* Exact match, not the case-insensitive compare the sort uses: this
         * answers "is the very same file still here", and on a FAT volume two
         * names differing only in case are the same file anyway. */
        if (entry != NULL && strcmp(entry->name, name) == 0) return index;
    }
    return count;
}

size_t file_browser_dir_next_file(const file_browser_dir_t *dir, size_t from)
{
    if (dir == NULL) return 0U;
    for (size_t i = from; i < dir->count; ++i) {
        if (dir->entries[i].kind == FILE_BROWSER_ENTRY_FILE) return i;
    }
    return dir->count;
}

size_t file_browser_dir_previous_file(const file_browser_dir_t *dir, size_t before)
{
    if (dir == NULL) return 0U;
    /* "None" is the entry count, the same answer next_file gives when it runs
     * out - so a caller that stops on "not a valid index" stops at either end
     * of the directory without knowing which end it hit. */
    size_t index = before < dir->count ? before : dir->count;
    while (index > 0U) {
        --index;
        if (dir->entries[index].kind == FILE_BROWSER_ENTRY_FILE) return index;
    }
    return dir->count;
}

/* A path is a mount root when it names a volume and nothing inside it:
 * "/usb0" and "/sd0" are roots, "/usb0/Music" is not.
 *
 * Written as "one slash, at the front" rather than compared against a list of
 * mount points, so that nothing here has to know which volumes the device has.
 * Browsing up out of a root then fails for the right reason - there is no
 * second slash to cut the path at. */
bool file_browser_path_is_root(const char *path)
{
    return path != NULL && path[0] == '/' && path[1] != '\0' &&
           strchr(path + 1, '/') == NULL;
}

bool file_browser_path_volume_root(const char *path, char *out, size_t out_size)
{
    if (path == NULL || out == NULL || out_size == 0U || path[0] != '/' ||
        path[1] == '\0') {
        return false;
    }
    const char *slash = strchr(path + 1, '/');
    const size_t length = slash == NULL ? strlen(path) : (size_t)(slash - path);
    if (length + 1U > out_size) return false;
    memcpy(out, path, length);
    out[length] = '\0';
    return true;
}

bool file_browser_path_on_volume(const char *path, const char *root)
{
    if (path == NULL || root == NULL) return false;
    const size_t length = strlen(root);
    return strncmp(path, root, length) == 0 &&
           (path[length] == '\0' || path[length] == '/');
}

bool file_browser_path_child(const char *path, const char *name, char *out, size_t out_size)
{
    if (path == NULL || name == NULL || out == NULL || out_size == 0U) return false;
    // "." and ".." would walk out of the mount point; nothing above a volume
    // root is ours to browse.
    if (file_browser_name_is_hidden(name) || strchr(name, '/') != NULL) return false;
    const size_t path_length = strlen(path);
    const size_t name_length = strlen(name);
    if (path_length + 1U + name_length + 1U > out_size) return false;
    memcpy(out, path, path_length);
    out[path_length] = '/';
    memcpy(out + path_length + 1U, name, name_length + 1U);
    return true;
}

bool file_browser_path_parent(const char *path, char *out, size_t out_size)
{
    if (path == NULL || out == NULL || out_size == 0U || file_browser_path_is_root(path)) {
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
