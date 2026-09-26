/* The .cue reader: a real sheet, then the encodings and the sloppy writers. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cue_sheet.h"
#include "fixtures/cue_nat_king_cole.h"

static uint32_t at(unsigned minutes, unsigned seconds, unsigned frames)
{
    return ((uint32_t)minutes * 60U + seconds) * CUE_SHEET_FRAMES_PER_SECOND + frames;
}

static cue_sheet_t *parse_text(const char *text)
{
    cue_sheet_t *sheet = malloc(sizeof(*sheet));
    assert(sheet != NULL);
    (void)cue_sheet_parse((const uint8_t *)text, strlen(text), sheet);
    return sheet;
}

static void test_a_real_lp_sheet(void)
{
    cue_sheet_t *sheet = malloc(sizeof(*sheet));
    assert(cue_sheet_parse(cue_nat_king_cole, sizeof(cue_nat_king_cole), sheet));
    assert(strcmp(sheet->performer, "Nat King Cole") == 0);
    assert(strcmp(sheet->title, "Sings The Blues") == 0);
    assert(sheet->file_count == 2U);
    assert(strcmp(sheet->files[0], "01. Side 1.flac") == 0);
    assert(strcmp(sheet->files[1], "02. Side 2.flac") == 0);
    assert(sheet->track_count == 12U && sheet->dropped == 0U);

    assert(sheet->tracks[0].number == 1U && sheet->tracks[0].file == 0U);
    assert(sheet->tracks[0].start_frames == 0U);
    assert(strcmp(sheet->tracks[0].title, "A1 Joe Turner Blues") == 0);
    assert(sheet->tracks[1].start_frames == at(2, 49, 25));
    assert(sheet->tracks[5].start_frames == at(13, 21, 25));
    assert(sheet->tracks[6].number == 7U && sheet->tracks[6].file == 1U);
    assert(sheet->tracks[6].start_frames == 0U);
    assert(strcmp(sheet->tracks[11].title, "B6 Careless Love") == 0);
    assert(sheet->tracks[9].start_frames == at(7, 49, 64));

    // Where each ends: the next start in the same file, or the file's end.
    assert(cue_sheet_track_end_frames(sheet, 0U) == at(2, 49, 25));
    assert(cue_sheet_track_end_frames(sheet, 5U) == 0U);   // last of side 1
    assert(cue_sheet_track_end_frames(sheet, 6U) == at(2, 33, 57));
    assert(cue_sheet_track_end_frames(sheet, 11U) == 0U);  // last of all
    free(sheet);
}

static void test_the_encodings(void)
{
    char out[256];
    // CP1252: the en dash and an accent, not Cyrillic.
    const uint8_t western[] = {'A', ' ', 0x96, ' ', 'C', 'a', 'f', 0xE9};
    cue_sheet_to_utf8(western, sizeof(western), out, sizeof(out));
    assert(strcmp(out, "A \xE2\x80\x93 Caf\xC3\xA9") == 0);

    // CP1251: a Russian word, and the en dash is still a dash.
    const uint8_t russian[] = {0xCA, 0xE8, 0xED, 0xEE, ' ', 0x96, ' ', 0xB9};
    cue_sheet_to_utf8(russian, sizeof(russian), out, sizeof(out));
    assert(strcmp(out, "\xD0\x9A\xD0\xB8\xD0\xBD\xD0\xBE \xE2\x80\x93 \xE2\x84\x96") == 0);

    // UTF-8 with a BOM: the BOM goes, the text stays as it is.
    const uint8_t utf8[] = {0xEF, 0xBB, 0xBF, 0xD0, 0x9A, 'x'};
    cue_sheet_to_utf8(utf8, sizeof(utf8), out, sizeof(out));
    assert(strcmp(out, "\xD0\x9Ax") == 0);

    // A Russian sheet parses into UTF-8 titles.
    const uint8_t sheet_bytes[] = "PERFORMER \"\xC0\xEB\xE8\xF1\xE0\"\r\n"
                                  "FILE \"a.flac\" WAVE\r\n"
                                  "  TRACK 01 AUDIO\r\n"
                                  "    TITLE \"\xCF\xE5\xF1\xED\xFF\"\r\n"
                                  "    INDEX 01 00:00:00\r\n";
    cue_sheet_t *sheet = malloc(sizeof(*sheet));
    assert(cue_sheet_parse(sheet_bytes, sizeof(sheet_bytes) - 1U, sheet));
    assert(strcmp(sheet->performer, "\xD0\x90\xD0\xBB\xD0\xB8\xD1\x81\xD0\xB0") == 0);
    assert(strcmp(sheet->tracks[0].title, "\xD0\x9F\xD0\xB5\xD1\x81\xD0\xBD\xD1\x8F") == 0);
    free(sheet);
}

static void test_what_is_kept_and_what_is_dropped(void)
{
    cue_sheet_t *sheet = parse_text(
        "FILE disc one.flac WAVE\n"          // unquoted, with a space
        "TRACK 01 AUDIO\n"
        "  PERFORMER \"Guest\"\n"
        "  INDEX 00 00:00:00\n"
        "  INDEX 01 00:02:00\n"
        "TRACK 02 AUDIO\n"                     // no INDEX 01: dropped
        "  INDEX 00 01:00:00\n"
        "TRACK 03 MODE1/2352\n"                // a data track: dropped
        "  INDEX 01 02:00:00\n"
        "FILE \"CD2\\side b.wav\" WAVE\n"      // a backslash from Windows
        "TRACK 04 AUDIO\n"
        "  INDEX 01 100:00:74\n");             // minutes past 99
    assert(sheet->track_count == 2U && sheet->dropped == 2U);
    assert(strcmp(sheet->files[0], "disc one.flac") == 0);
    assert(strcmp(sheet->files[1], "CD2/side b.wav") == 0);
    assert(sheet->tracks[0].start_frames == at(0, 2, 0));
    assert(strcmp(sheet->tracks[0].performer, "Guest") == 0);
    assert(sheet->tracks[1].number == 4U && sheet->tracks[1].file == 1U);
    assert(sheet->tracks[1].start_frames == at(100, 0, 74));
    free(sheet);

    // Nothing playable at all.
    sheet = parse_text("REM just a comment\nTITLE \"x\"\n");
    assert(sheet->track_count == 0U);
    free(sheet);
    cue_sheet_t empty;
    assert(!cue_sheet_parse((const uint8_t *)"", 0U, &empty));
}

int main(void)
{
    test_a_real_lp_sheet();
    test_the_encodings();
    test_what_is_kept_and_what_is_dropped();
    puts("cue sheet tests passed");
    return 0;
}
