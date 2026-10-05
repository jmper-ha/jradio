#include "ui_theme.h"

#include <stdio.h>
#include <string.h>

/* The panel as it was before themes: every value is the constant ui.c used to
 * carry, so the standard theme cannot move a pixel. The notes are the ones
 * those constants had. */
static const ui_palette_t k_standard = {.rgb = {
    [UI_ROLE_GROUND] = 0x101820,
    [UI_ROLE_TEXT] = 0xFFFFFF,
    /* The artist was the secondary grey until it got a role of its own. */
    [UI_ROLE_ARTIST] = 0xB0BEC5,
    [UI_ROLE_SECONDARY] = 0xB0BEC5,
    [UI_ROLE_DIM] = 0x78909C,
    [UI_ROLE_ACCENT] = 0xF2A33C,
    [UI_ROLE_STRIP] = 0x1E2C3A,
    [UI_ROLE_TILE] = 0x18242E,
    [UI_ROLE_TILE_EDGE] = 0x26343F,
    [UI_ROLE_RULE] = 0x334454,
    /* The hairline between list rows: only just off the ground, so the rows
     * read as separate without a grid being drawn over them. */
    [UI_ROLE_DIVIDER] = 0x243240,
    /* The settings rows: tinted tiles rather than bare ground. */
    [UI_ROLE_FIELD_ROW] = 0x1D2A36,
    /* A raised step rather than a colour of its own: the row under the cursor
     * is lifted off the ground and its text takes the accent, which is how
     * the player screen marks the thing being played. */
    [UI_ROLE_SELECTED] = 0x2A3B4A,
    /* The settings screen needs a brighter cursor than the menu's. Its rows
     * are already tinted tiles, and SELECTED lands within a few percent of
     * the tile under it - close enough that the cursor could not be found
     * without moving it. This is well clear of both. */
    [UI_ROLE_CURSOR] = 0x3F6187,
    /* A row that is on the screen but cannot be started: dimmer than the
     * unselected text, still plainly readable against the ground. */
    [UI_ROLE_DISABLED] = 0x4E606C,
    /* The empty part of the progress and volume bars, and of the list's
     * position bar. */
    [UI_ROLE_TRACK] = 0x23303C,
    /* The Wi-Fi bars that are not lit. */
    [UI_ROLE_BAR_OFF] = 0x2D3F4D,
    /* Unlit VU blocks stay visible, so the meter reads as a scale at rest
     * instead of an empty strip. */
    [UI_ROLE_VU_OFF] = 0x263746,
    /* The unlit steps of the FM signal mark: there, but off. */
    [UI_ROLE_FM_UNLIT] = 0x37474F,
    /* The note on an empty cover tile. */
    [UI_ROLE_ART_NOTE] = 0x3E5060,
    /* Depth in the carousel is carried by brightness as well as size:
     * without it the middle icon reads as the only lit one rather than as
     * the middle of a ring. The dots under it are dimmer again. */
    [UI_ROLE_FEED_NEAR] = 0x8FA8BC,
    [UI_ROLE_FEED_FAR] = 0x46586A,
    [UI_ROLE_FEED_DOT] = 0x33445A,
}};

/* Black ground, near-white text: for the panels whose glass washes the greys
 * out. Its surfaces are derived like a custom theme's. */
static const uint32_t k_contrast_base[UI_THEME_BASE_ROLES] = {
    [UI_ROLE_GROUND] = 0x000000,
    [UI_ROLE_TEXT] = 0xFFFFFF,
    [UI_ROLE_ARTIST] = 0xE8E8E8,
    [UI_ROLE_SECONDARY] = 0xE0E6EA,
    [UI_ROLE_DIM] = 0xB0BEC5,
    [UI_ROLE_ACCENT] = 0xFFB74D,
};

uint32_t ui_theme_mix(uint32_t from, uint32_t to, unsigned weight)
{
    if (weight > 256U) weight = 256U;
    uint32_t out = 0U;
    for (unsigned shift = 0U; shift <= 16U; shift += 8U) {
        const unsigned a = (from >> shift) & 0xFFU;
        const unsigned b = (to >> shift) & 0xFFU;
        const unsigned c = (a * (256U - weight) + b * weight + 128U) / 256U;
        out |= (uint32_t)c << shift;
    }
    return out;
}

/* How far each surface sits from the ground towards the text, out of 256.
 * Chosen to land near the standard theme's steps, which a mix cannot copy
 * exactly - its surfaces lean blue - and to keep the same order: a tile below
 * a strip below a rule, the cursor clearly above the selected row. Being
 * fractions of the distance, they work the same way round on a light ground
 * with dark text. */
void ui_theme_derive(const uint32_t base[UI_THEME_BASE_ROLES], ui_palette_t *palette)
{
    if (base == NULL || palette == NULL) return;
    for (unsigned role = 0U; role < UI_THEME_BASE_ROLES; ++role) {
        palette->rgb[role] = base[role] & 0xFFFFFFU;
    }
    const uint32_t ground = palette->rgb[UI_ROLE_GROUND];
    const uint32_t text = palette->rgb[UI_ROLE_TEXT];
    const uint32_t secondary = palette->rgb[UI_ROLE_SECONDARY];
    const uint32_t dim = palette->rgb[UI_ROLE_DIM];
    palette->rgb[UI_ROLE_TILE] = ui_theme_mix(ground, text, 14U);
    palette->rgb[UI_ROLE_FIELD_ROW] = ui_theme_mix(ground, text, 20U);
    palette->rgb[UI_ROLE_DIVIDER] = ui_theme_mix(ground, text, 34U);
    palette->rgb[UI_ROLE_STRIP] = ui_theme_mix(ground, text, 26U);
    palette->rgb[UI_ROLE_TRACK] = ui_theme_mix(ground, text, 30U);
    palette->rgb[UI_ROLE_VU_OFF] = ui_theme_mix(ground, text, 30U);
    palette->rgb[UI_ROLE_TILE_EDGE] = ui_theme_mix(ground, text, 34U);
    palette->rgb[UI_ROLE_SELECTED] = ui_theme_mix(ground, text, 38U);
    palette->rgb[UI_ROLE_BAR_OFF] = ui_theme_mix(ground, text, 40U);
    palette->rgb[UI_ROLE_FEED_DOT] = ui_theme_mix(ground, text, 44U);
    palette->rgb[UI_ROLE_RULE] = ui_theme_mix(ground, text, 48U);
    palette->rgb[UI_ROLE_FM_UNLIT] = ui_theme_mix(ground, text, 48U);
    palette->rgb[UI_ROLE_ART_NOTE] = ui_theme_mix(ground, text, 62U);
    palette->rgb[UI_ROLE_CURSOR] = ui_theme_mix(ground, text, 72U);
    palette->rgb[UI_ROLE_DISABLED] = ui_theme_mix(ground, dim, 140U);
    palette->rgb[UI_ROLE_FEED_FAR] = ui_theme_mix(ground, secondary, 96U);
    palette->rgb[UI_ROLE_FEED_NEAR] = ui_theme_mix(ground, secondary, 192U);
}

void ui_theme_preset(ui_theme_preset_t preset, ui_palette_t *palette)
{
    if (palette == NULL) return;
    if (preset == UI_THEME_CONTRAST) {
        ui_theme_derive(k_contrast_base, palette);
        return;
    }
    *palette = k_standard;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool ui_theme_parse_hex(const char *text, uint32_t *rgb)
{
    if (text == NULL || rgb == NULL) return false;
    if (text[0] == '#') ++text;
    if (strlen(text) != 6U) return false;
    uint32_t value = 0U;
    for (size_t i = 0U; i < 6U; ++i) {
        const int digit = hex_digit(text[i]);
        if (digit < 0) return false;
        value = (value << 4) | (uint32_t)digit;
    }
    *rgb = value;
    return true;
}

void ui_theme_format_hex(uint32_t rgb, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0U) return;
    snprintf(out, out_size, "#%06X", (unsigned)(rgb & 0xFFFFFFU));
}
