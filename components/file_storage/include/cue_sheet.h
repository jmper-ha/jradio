#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Reading a .cue sheet: an album ripped as one file per disc side (or one for
 * the whole disc) with a text file saying where each track starts in it.
 *
 * Pure, like playlist_file.c, so the awkward parts - the text encoding, the
 * quoting, the MM:SS:FF times - are tested on the host. Nothing here opens a
 * file: the caller hands over the sheet's bytes and gets the tracks back.
 *
 * Only what playing needs is read: FILE, TRACK, TITLE, PERFORMER and INDEX 01.
 * INDEX 00 (the pregap) is left to the track before it, which is where a
 * player that plays straight through puts it anyway. */

#define CUE_SHEET_TRACKS_MAX 99U
#define CUE_SHEET_FILES_MAX 16U
#define CUE_SHEET_TEXT_MAX 128U
// A FILE name as written, relative to the sheet's own folder - the same
// meaning a playlist reference has, and the same length limit.
#define CUE_SHEET_FILE_NAME_MAX 256U
// CD frames, the unit INDEX times are written in: 75 to the second.
#define CUE_SHEET_FRAMES_PER_SECOND 75U

typedef struct {
    uint8_t number;
    uint8_t file;             // index into cue_sheet_t.files
    uint32_t start_frames;    // INDEX 01, from the start of its file
    char title[CUE_SHEET_TEXT_MAX];
    char performer[CUE_SHEET_TEXT_MAX];
} cue_sheet_track_t;

typedef struct {
    char title[CUE_SHEET_TEXT_MAX];      // the album
    char performer[CUE_SHEET_TEXT_MAX];  // the album's
    char files[CUE_SHEET_FILES_MAX][CUE_SHEET_FILE_NAME_MAX];
    size_t file_count;
    cue_sheet_track_t tracks[CUE_SHEET_TRACKS_MAX];
    size_t track_count;
    /* Tracks the sheet named but that were not kept: no INDEX 01, a DATA
     * track, one past the limit. Counted so a short list has a reason. */
    size_t dropped;
} cue_sheet_t;

/* Parses a sheet. `text` is the file's bytes, in whatever encoding the ripper
 * wrote them: UTF-8 (with or without a BOM) is taken as it is; anything else is
 * Windows-1251 when it carries Russian words and Windows-1252 otherwise - the
 * two code pages rippers on this side of the world write in. False when no
 * track came out of it at all.
 *
 * The sheet is about 30 KB, so it is the caller's to allocate - from PSRAM on
 * the device, never on a task's stack. */
bool cue_sheet_parse(const uint8_t *text, size_t length, cue_sheet_t *out);

/* Converts the sheet's bytes to UTF-8 by the rule above. Exposed for the tests:
 * it is the one guess in the parser. Returns the bytes written, not counting
 * the terminator; always terminates when `capacity` is non-zero. */
size_t cue_sheet_to_utf8(const uint8_t *text, size_t length, char *out, size_t capacity);

/* Where a track stops in its file, in CD frames: the next track's start when
 * it is in the same file, 0 when the track runs to the end of the file. */
uint32_t cue_sheet_track_end_frames(const cue_sheet_t *sheet, size_t index);

#ifdef __cplusplus
}
#endif
