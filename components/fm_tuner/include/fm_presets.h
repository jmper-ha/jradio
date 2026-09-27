#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The FM presets: fm_presets.csv, one station a line, in the user's order.
 *
 *     name<TAB>frequency in kHz[<TAB>picture]
 *
 * The same shape as a playlist line, a tab between the columns because a
 * name may hold a comma. The frequency is a plain number of kHz, not "101.2",
 * so there is one spelling of it; the picture is a file name in radio_img,
 * the same directory and the same rules as a station's, and optional.
 *
 * Kept on the data partition and written by the web page's FM section, which
 * scans and names them; the device only plays them. Pure: the parsing and the
 * writing are tested on the host. */
#define FM_PRESETS_PATH "/littlefs/config/fm_presets.csv"
#define FM_PRESETS_TEMP_PATH "/littlefs/config/fm_presets.tmp"
#define FM_PRESETS_MAX 40
#define FM_PRESET_NAME_MAX_LEN 48
#define FM_PRESET_ICON_MAX_LEN 32
// name, tab, six digits, tab, picture, newline, and a margin.
#define FM_PRESETS_LINE_MAX_LEN (FM_PRESET_NAME_MAX_LEN + FM_PRESET_ICON_MAX_LEN + 12U)
#define FM_PRESETS_TEXT_MAX_LEN (FM_PRESETS_MAX * FM_PRESETS_LINE_MAX_LEN + 1U)

typedef struct {
    char name[FM_PRESET_NAME_MAX_LEN];
    uint32_t khz;
    char icon[FM_PRESET_ICON_MAX_LEN];
} fm_preset_t;

typedef struct {
    fm_preset_t presets[FM_PRESETS_MAX];
    size_t count;
} fm_presets_t;

/* Reads the whole file. A line that does not parse - a frequency off the
 * band, a picture name that would leave its directory, no tab at all - is
 * skipped and counted, never fatal: one bad line must not cost the rest.
 * Past FM_PRESETS_MAX the rest is dropped. Returns the lines skipped. */
size_t fm_presets_parse(const char *text, size_t length, fm_presets_t *presets);

/* The whole list back as text, the file's own shape. Returns the length it
 * needed, like snprintf; cut but terminated when that is over `capacity`. */
size_t fm_presets_write(const fm_presets_t *presets, char *out, size_t capacity);

/* The preset on `khz`, or false. */
bool fm_presets_find(const fm_presets_t *presets, uint32_t khz, size_t *index);

#ifdef __cplusplus
}
#endif
