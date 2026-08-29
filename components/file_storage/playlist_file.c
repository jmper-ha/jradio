#include "playlist_file.h"

#include <string.h>

static char ascii_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/* Matched on the tail rather than through an extension scanner, so that a name
 * that is nothing but the extension - ".m3u" - does not match: that is a
 * hidden file, and the browser drops it before this is ever asked. */
static bool ends_with_ci(const char *name, const char *suffix)
{
    const size_t name_length = strlen(name);
    const size_t suffix_length = strlen(suffix);
    if (suffix_length >= name_length) return false;
    const char *tail = name + (name_length - suffix_length);
    for (size_t i = 0U; i < suffix_length; ++i) {
        if (ascii_lower(tail[i]) != ascii_lower(suffix[i])) return false;
    }
    return true;
}

playlist_file_kind_t playlist_file_kind_from_name(const char *name)
{
    if (name == NULL) return PLAYLIST_FILE_NONE;
    if (ends_with_ci(name, ".m3u") || ends_with_ci(name, ".m3u8")) return PLAYLIST_FILE_M3U;
    if (ends_with_ci(name, ".pls")) return PLAYLIST_FILE_PLS;
    return PLAYLIST_FILE_NONE;
}

const char *playlist_file_kind_name(playlist_file_kind_t kind)
{
    switch (kind) {
    case PLAYLIST_FILE_M3U: return "M3U";
    case PLAYLIST_FILE_PLS: return "PLS";
    case PLAYLIST_FILE_NONE:
    default: return "";
    }
}

// Leading blanks skipped, everything from the last non-blank onwards cut. The
// cut end is what removes the CR of a file written on Windows, which is most
// of them - a reference with a stray CR opens nothing and says nothing.
static char *trim(char *line)
{
    while (*line == ' ' || *line == '\t') ++line;
    size_t length = strlen(line);
    while (length > 0U && (unsigned char)line[length - 1U] <= ' ') --length;
    line[length] = '\0';
    return line;
}

/* The value of a "File12=" line, or NULL for any other key.
 *
 * Only File carries a path: Title, Length, NumberOfEntries and Version do not,
 * and neither does the "[playlist]" header. The number is not checked against
 * anything - the entry order in the file is the playing order, and a file
 * whose numbering skips or repeats still names its tracks in the order they
 * are written. */
static char *pls_file_value(char *line)
{
    static const char key[] = "file";
    for (size_t i = 0U; i < sizeof(key) - 1U; ++i) {
        // A short line stops here on its terminator, which matches no key
        // character, so this never reads past the end.
        if (ascii_lower(line[i]) != key[i]) return NULL;
    }
    char *cursor = line + (sizeof(key) - 1U);
    if (*cursor < '0' || *cursor > '9') return NULL;
    while (*cursor >= '0' && *cursor <= '9') ++cursor;
    if (*cursor != '=') return NULL;
    ++cursor;
    while (*cursor == ' ' || *cursor == '\t') ++cursor;
    return cursor;
}

/* Turns a path as written into the one shape the rest of the code handles:
 * forward slashes, no doubled ones, no leading "./".
 *
 * Playlists are written by desktop players on Windows, which put backslashes
 * in - and a backslash is an ordinary character in a FAT name, so left alone
 * it becomes part of the file name being looked for rather than a separator.
 *
 * Normalising here rather than at play time is what lets a track be found
 * again in the listing it came from: the reference is stored as the entry's
 * name, and the same input has to give the same name every time. */
static char *normalize(char *reference)
{
    for (char *c = reference; *c != '\0'; ++c) {
        if (*c == '\\') *c = '/';
    }
    char *write = reference;
    for (const char *read = reference; *read != '\0'; ++read) {
        if (*read == '/' && write > reference && write[-1] == '/') continue;
        *write++ = *read;
    }
    *write = '\0';
    while (reference[0] == '.' && reference[1] == '/') reference += 2;
    return reference;
}

bool playlist_file_reference_is_safe(const char *reference)
{
    if (reference == NULL || reference[0] == '\0') return false;
    /* One rule covers both a URL and a Windows drive letter: a colon cannot
     * occur in a FAT name, so "http://stream" and "D:/music/1.mp3" are refused
     * together. The stream would need the radio source, not the file player;
     * the drive letter names a disk that is not here. */
    if (strchr(reference, ':') != NULL) return false;
    for (const char *component = reference;;) {
        const char *end = strchr(component, '/');
        const size_t length = end == NULL ? strlen(component) : (size_t)(end - component);
        // An empty component is only allowed at the very front, where it is
        // the leading slash of a full path. Anywhere else it means a trailing
        // slash - a directory, which is not a track.
        if (length == 0U && component != reference) return false;
        // ".." would climb out of the volume, and "." is the normaliser's job
        // to have removed. Neither is something to open.
        if (length == 1U && component[0] == '.') return false;
        if (length == 2U && component[0] == '.' && component[1] == '.') return false;
        if (end == NULL) break;
        component = end + 1;
    }
    return true;
}

playlist_file_line_t playlist_file_read_line(playlist_file_kind_t kind, char *line,
                                             const char **reference)
{
    if (line == NULL || reference == NULL) return PLAYLIST_FILE_LINE_IGNORED;
    *reference = NULL;
    char *text = trim(line);
    if (text[0] == '\0') return PLAYLIST_FILE_LINE_IGNORED;
    if (kind == PLAYLIST_FILE_M3U) {
        /* "#EXTM3U", "#EXTINF:" and every other directive. The title an
         * #EXTINF line carries is read and dropped: holding it would mean a
         * second string per entry in the one listing the device has, and the
         * player screen names the track from its tags anyway. */
        if (text[0] == '#') return PLAYLIST_FILE_LINE_IGNORED;
    } else if (kind == PLAYLIST_FILE_PLS) {
        text = pls_file_value(text);
        if (text == NULL) return PLAYLIST_FILE_LINE_IGNORED;
    } else {
        return PLAYLIST_FILE_LINE_IGNORED;
    }
    char *normalized = normalize(text);
    if (normalized[0] == '\0') return PLAYLIST_FILE_LINE_IGNORED;
    if (!playlist_file_reference_is_safe(normalized)) return PLAYLIST_FILE_LINE_UNPLAYABLE;
    *reference = normalized;
    return PLAYLIST_FILE_LINE_TRACK;
}
