#include "cue_sheet.h"

#include <string.h>

/* ---- text encoding ---------------------------------------------------- */

/* Windows-1251, the upper half. Unlike the tag reader's rule, which only has
 * to recognise Cyrillic letters, a sheet needs the punctuation too: the one
 * this was written for has no Russian in it at all and an en dash (0x96) in
 * the album name, which read as Latin-1 is a control code. */
static const uint16_t CP1251_HIGH[128] = {
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, // 80
    0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F, // 88
    0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, // 90
    0x0020, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F, // 98
    0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7, // A0
    0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407, // A8
    0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7, // B0
    0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457, // B8
    0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417, // C0
    0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F, // C8
    0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427, // D0
    0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F, // D8
    0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437, // E0
    0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F, // E8
    0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447, // F0
    0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F, // F8
};

/* Windows-1252 differs from Latin-1 only in 0x80..0x9F; the holes are shown
 * as spaces rather than dropped, so a word does not run into the next. */
static const uint16_t CP1252_C1[32] = {
    0x20AC, 0x0020, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, // 80
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x0020, 0x017D, 0x0020, // 88
    0x0020, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, // 90
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x0020, 0x017E, 0x0178, // 98
};

static bool valid_utf8(const uint8_t *text, size_t length)
{
    size_t i = 0U;
    while (i < length) {
        const uint8_t lead = text[i];
        size_t follow;
        if (lead < 0x80U) {
            follow = 0U;
        } else if (lead >= 0xC2U && lead <= 0xDFU) {
            follow = 1U;
        } else if (lead >= 0xE0U && lead <= 0xEFU) {
            follow = 2U;
        } else if (lead >= 0xF0U && lead <= 0xF4U) {
            follow = 3U;
        } else {
            return false;
        }
        if (i + follow >= length && follow > 0U) return false;
        for (size_t k = 1U; k <= follow; ++k) {
            if ((text[i + k] & 0xC0U) != 0x80U) return false;
        }
        i += follow + 1U;
    }
    return true;
}

static bool cp1251_letter(uint8_t value)
{
    return value == 0xA8U || value == 0xB8U || value >= 0xC0U;
}

/* Russian words, or Western text with an accent here and there? A Cyrillic
 * word is several letters from the upper half in a row; "Motörhead" or "Café"
 * put one between ASCII letters. Three in a row settles it. */
static bool looks_like_cp1251(const uint8_t *text, size_t length)
{
    size_t run = 0U;
    for (size_t i = 0U; i < length; ++i) {
        run = cp1251_letter(text[i]) ? run + 1U : 0U;
        if (run >= 3U) return true;
    }
    return false;
}

static bool put_utf8(char *out, size_t capacity, size_t *used, uint32_t code)
{
    uint8_t bytes[3];
    size_t width;
    if (code < 0x80U) {
        bytes[0] = (uint8_t)code;
        width = 1U;
    } else if (code < 0x800U) {
        bytes[0] = (uint8_t)(0xC0U | (code >> 6));
        bytes[1] = (uint8_t)(0x80U | (code & 0x3FU));
        width = 2U;
    } else {
        bytes[0] = (uint8_t)(0xE0U | (code >> 12));
        bytes[1] = (uint8_t)(0x80U | ((code >> 6) & 0x3FU));
        bytes[2] = (uint8_t)(0x80U | (code & 0x3FU));
        width = 3U;
    }
    if (*used + width + 1U > capacity) return false;
    memcpy(out + *used, bytes, width);
    *used += width;
    return true;
}

size_t cue_sheet_to_utf8(const uint8_t *text, size_t length, char *out, size_t capacity)
{
    if (out == NULL || capacity == 0U) return 0U;
    out[0] = '\0';
    if (text == NULL) return 0U;
    if (length >= 3U && text[0] == 0xEFU && text[1] == 0xBBU && text[2] == 0xBFU) {
        text += 3;
        length -= 3U;
    }
    size_t used = 0U;
    if (valid_utf8(text, length)) {
        // Cut on a character boundary, never inside one.
        size_t take = length < capacity - 1U ? length : capacity - 1U;
        while (take > 0U && take < length && (text[take] & 0xC0U) == 0x80U) --take;
        memcpy(out, text, take);
        out[take] = '\0';
        return take;
    }
    const bool cyrillic = looks_like_cp1251(text, length);
    for (size_t i = 0U; i < length; ++i) {
        const uint8_t value = text[i];
        uint32_t code = value;
        if (value >= 0x80U) {
            if (cyrillic) {
                code = CP1251_HIGH[value - 0x80U];
            } else if (value < 0xA0U) {
                code = CP1252_C1[value - 0x80U];
            }
        }
        if (!put_utf8(out, capacity, &used, code)) break;
    }
    out[used] = '\0';
    return used;
}

/* ---- the sheet -------------------------------------------------------- */

static bool is_space(char c) { return c == ' ' || c == '\t'; }

static char upper(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c; }

// Whether `line` starts with the command word `word`, case-insensitively,
// followed by a space or the end; on true, points `rest` past the spaces.
static bool command(const char *line, const char *word, const char **rest)
{
    size_t i = 0U;
    for (; word[i] != '\0'; ++i) {
        if (upper(line[i]) != word[i]) return false;
    }
    if (line[i] != '\0' && !is_space(line[i])) return false;
    while (is_space(line[i])) ++i;
    *rest = line + i;
    return true;
}

/* A quoted argument - FILE, TITLE, PERFORMER - or, from a sloppy writer, a
 * bare one. For FILE an unquoted name may hold spaces, so `drop_last_word`
 * takes everything up to the file type that ends the line. */
static void argument(const char *rest, bool drop_last_word, char *out, size_t size)
{
    size_t length;
    const char *start = rest;
    if (*rest == '"') {
        ++start;
        const char *end = strchr(start, '"');
        length = end != NULL ? (size_t)(end - start) : strlen(start);
    } else {
        length = strlen(rest);
        while (length > 0U && is_space(rest[length - 1U])) --length;
        if (drop_last_word) {
            size_t cut = length;
            while (cut > 0U && !is_space(rest[cut - 1U])) --cut;
            if (cut > 0U) {
                length = cut;
                while (length > 0U && is_space(rest[length - 1U])) --length;
            }
        }
    }
    // Cut on a character boundary: a title too long for its field costs its
    // tail, not a broken glyph at the end.
    if (length >= size) {
        length = size - 1U;
        while (length > 0U && ((uint8_t)start[length] & 0xC0U) == 0x80U) --length;
    }
    memcpy(out, start, length);
    out[length] = '\0';
}

static bool parse_number(const char **cursor, unsigned *value)
{
    const char *p = *cursor;
    if (*p < '0' || *p > '9') return false;
    unsigned n = 0U;
    while (*p >= '0' && *p <= '9') {
        n = n * 10U + (unsigned)(*p - '0');
        if (n > 100000U) return false;
        ++p;
    }
    *value = n;
    *cursor = p;
    return true;
}

// "MM:SS:FF" - minutes may run past 99 on a long file.
static bool parse_time(const char *text, uint32_t *frames)
{
    unsigned minutes;
    unsigned seconds;
    unsigned cd_frames;
    if (!parse_number(&text, &minutes) || *text++ != ':') return false;
    if (!parse_number(&text, &seconds) || *text++ != ':') return false;
    if (!parse_number(&text, &cd_frames)) return false;
    if (seconds >= 60U || cd_frames >= CUE_SHEET_FRAMES_PER_SECOND) return false;
    *frames = ((uint32_t)minutes * 60U + seconds) * CUE_SHEET_FRAMES_PER_SECOND + cd_frames;
    return true;
}

typedef struct {
    cue_sheet_t *sheet;
    bool in_track;       // a TRACK is open
    bool keep;           // ...and it is one worth keeping (AUDIO, has a FILE)
    bool has_start;
    bool file_ok;        // the last FILE fitted in the table
    cue_sheet_track_t pending;
} cue_parse_t;

static void close_track(cue_parse_t *p)
{
    if (!p->in_track) return;
    p->in_track = false;
    if (!p->keep || !p->has_start || p->sheet->track_count >= CUE_SHEET_TRACKS_MAX) {
        ++p->sheet->dropped;
        return;
    }
    p->sheet->tracks[p->sheet->track_count++] = p->pending;
}

static void parse_line(cue_parse_t *p, const char *line)
{
    cue_sheet_t *sheet = p->sheet;
    const char *rest;
    while (is_space(*line)) ++line;
    if (command(line, "FILE", &rest)) {
        close_track(p);
        if (sheet->file_count >= CUE_SHEET_FILES_MAX) {
            p->file_ok = false;
            return;
        }
        char *name = sheet->files[sheet->file_count];
        argument(rest, true, name, CUE_SHEET_FILE_NAME_MAX);
        // Written on Windows more often than not.
        for (char *c = name; *c != '\0'; ++c) {
            if (*c == '\\') *c = '/';
        }
        ++sheet->file_count;
        p->file_ok = name[0] != '\0';
    } else if (command(line, "TRACK", &rest)) {
        close_track(p);
        unsigned number;
        memset(&p->pending, 0, sizeof(p->pending));
        p->in_track = true;
        p->has_start = false;
        p->keep = sheet->file_count > 0U && p->file_ok && parse_number(&rest, &number) &&
                  number >= 1U && number <= 99U;
        while (is_space(*rest)) ++rest;
        const char *type;
        // DATA tracks on an enhanced CD are not audio.
        if (!command(rest, "AUDIO", &type)) p->keep = false;
        if (p->keep) {
            p->pending.number = (uint8_t)number;
            p->pending.file = (uint8_t)(sheet->file_count - 1U);
        }
    } else if (command(line, "TITLE", &rest)) {
        argument(rest, false, p->in_track ? p->pending.title : sheet->title, CUE_SHEET_TEXT_MAX);
    } else if (command(line, "PERFORMER", &rest)) {
        argument(rest, false, p->in_track ? p->pending.performer : sheet->performer,
                 CUE_SHEET_TEXT_MAX);
    } else if (command(line, "INDEX", &rest) && p->in_track) {
        unsigned index;
        if (!parse_number(&rest, &index) || index != 1U) return;
        while (is_space(*rest)) ++rest;
        uint32_t frames;
        if (parse_time(rest, &frames)) {
            p->pending.start_frames = frames;
            p->has_start = true;
        }
    }
    // REM, CATALOG, FLAGS, ISRC, PREGAP, SONGWRITER...: nothing to play.
}

bool cue_sheet_parse(const uint8_t *text, size_t length, cue_sheet_t *out)
{
    if (out == NULL) return false;
    memset(out, 0, sizeof(*out));
    if (text == NULL || length == 0U) return false;
    cue_parse_t p = {.sheet = out};

    /* Converted a line at a time rather than the whole file at once, so the
     * only buffer is one line long; the encoding is decided on the whole file,
     * though - one line of a CP1251 sheet can look like anything. */
    const bool utf8_whole = valid_utf8(text, length) ||
                            (length >= 3U && text[0] == 0xEFU && text[1] == 0xBBU &&
                             text[2] == 0xBFU);
    size_t start = 0U;
    if (length >= 3U && text[0] == 0xEFU && text[1] == 0xBBU && text[2] == 0xBFU) start = 3U;
    const bool cyrillic = !utf8_whole && looks_like_cp1251(text, length);
    char line[CUE_SHEET_FILE_NAME_MAX * 3U + 64U];
    while (start < length) {
        size_t end = start;
        while (end < length && text[end] != '\n' && text[end] != '\r') ++end;
        // Each line decoded with the file's own verdict, not its own.
        size_t used = 0U;
        for (size_t i = start; i < end; ++i) {
            const uint8_t value = text[i];
            if (utf8_whole || value < 0x80U) {
                if (used + 2U > sizeof(line)) break;
                line[used++] = (char)value;
                continue;
            }
            uint32_t code = value;
            if (cyrillic) {
                code = CP1251_HIGH[value - 0x80U];
            } else if (value < 0xA0U) {
                code = CP1252_C1[value - 0x80U];
            }
            if (!put_utf8(line, sizeof(line), &used, code)) break;
        }
        line[used] = '\0';
        parse_line(&p, line);
        start = end;
        while (start < length && (text[start] == '\n' || text[start] == '\r')) ++start;
    }
    close_track(&p);
    return out->track_count > 0U;
}

uint32_t cue_sheet_track_end_frames(const cue_sheet_t *sheet, size_t index)
{
    if (sheet == NULL || index + 1U >= sheet->track_count) return 0U;
    const cue_sheet_track_t *here = &sheet->tracks[index];
    const cue_sheet_track_t *next = &sheet->tracks[index + 1U];
    return next->file == here->file && next->start_frames > here->start_frames
               ? next->start_frames
               : 0U;
}
