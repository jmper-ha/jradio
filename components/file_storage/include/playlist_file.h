#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Reading .m3u and .pls files: the line syntax and nothing else.
 *
 * Kept apart from file_storage.c so the awkward half - what counts as a
 * comment, which lines carry a path, what a path written on another machine
 * has to be turned into - is testable on the host with no FATFS in reach.
 *
 * Both formats are line-based and both are read the same way: a line goes in,
 * either a track reference comes out or the line was not one. Nothing here
 * opens a file or resolves a path against a volume. */

typedef enum {
    PLAYLIST_FILE_NONE = 0,
    PLAYLIST_FILE_M3U,
    PLAYLIST_FILE_PLS,
} playlist_file_kind_t;

// By extension: .m3u and .m3u8 are M3U, .pls is PLS. ".m3u8" is not a separate
// format - it is an M3U that promises to be UTF-8, which is the only encoding
// this reads anyway.
playlist_file_kind_t playlist_file_kind_from_name(const char *name);
// "M3U" / "PLS" / "", for the type column on the browser row.
const char *playlist_file_kind_name(playlist_file_kind_t kind);

typedef enum {
    // A comment, a header, a blank line, or - in a .pls - one of the fields
    // that is not a path. Nothing to report: the file is as expected.
    PLAYLIST_FILE_LINE_IGNORED = 0,
    PLAYLIST_FILE_LINE_TRACK,
    /* A reference this device cannot open: a http:// stream, a Windows drive
     * letter, or a path that climbs out of the volume with "..". Told apart
     * from IGNORED so the log can say how many tracks a playlist lost, rather
     * than showing a short list with no explanation. */
    PLAYLIST_FILE_LINE_UNPLAYABLE,
} playlist_file_line_t;

/* Reads one line, rewriting it in place, and points `reference` at the track
 * path it carries.
 *
 * In place because the alternative is a second 1 KB buffer on a task whose
 * stack is already the tightest on the device: trimming, the backslash
 * rewrite, and dropping a "File3=" prefix all only ever shorten the line.
 * `reference` therefore lives exactly as long as `line` does.
 *
 * The reference that comes back is relative to the directory the playlist file
 * itself is in, unless it starts with '/', in which case it is a full path on
 * a volume. Both are what file_browser_dir_path_for() expects. */
playlist_file_line_t playlist_file_read_line(playlist_file_kind_t kind, char *line,
                                             const char **reference);

/* True when a reference is one this device may open.
 *
 * Checked again where a reference is turned into a path, not only where it is
 * parsed: a playlist is a file on a drive somebody else wrote, and "climb out
 * of the volume" must fail at the point where the answer is used, whatever
 * path the entry took to get there. */
bool playlist_file_reference_is_safe(const char *reference);

#ifdef __cplusplus
}
#endif
