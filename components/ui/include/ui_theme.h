#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The panel's colours, as roles rather than constants.
 *
 * Six of them are what a person chooses - the ground, the main text, the
 * artist on the player, the secondary text, the dim text and the accent. The
 * rest follow from those: the strips, rules and tiles a few steps off the
 * ground, the cursor, the tracks of the progress and volume bars. A theme is
 * therefore six colours, and the panel never shows a surface that was not
 * worked out from them - which is what stops a hand-picked theme from losing
 * its cursor against its own rows.
 *
 * The standard theme is the exception, deliberately: every one of its colours
 * is the value the panel had before themes existed, written out, so choosing
 * nothing changes nothing by a pixel. Its derived colours have a blue cast no
 * mix of its six base colours reproduces.
 *
 * Colours that mean something stay out of this - an error is red and
 * Bluetooth is blue whatever the ground is - and so does the VU meter's green
 * and red. */
typedef enum {
    UI_ROLE_GROUND = 0,
    UI_ROLE_TEXT,
    UI_ROLE_ARTIST,
    UI_ROLE_SECONDARY,
    UI_ROLE_DIM,
    UI_ROLE_ACCENT,
    /* Derived from the six above. */
    UI_ROLE_STRIP,
    UI_ROLE_TILE,
    UI_ROLE_TILE_EDGE,
    UI_ROLE_RULE,
    UI_ROLE_FIELD_ROW,
    UI_ROLE_SELECTED,
    UI_ROLE_CURSOR,
    UI_ROLE_DISABLED,
    UI_ROLE_TRACK,
    UI_ROLE_BAR_OFF,
    UI_ROLE_VU_OFF,
    UI_ROLE_FM_UNLIT,
    UI_ROLE_ART_NOTE,
    UI_ROLE_FEED_NEAR,
    UI_ROLE_FEED_FAR,
    UI_ROLE_FEED_DOT,
    UI_ROLE_COUNT,
} ui_role_t;

/* The roles a person picks; the rest are derived. */
#define UI_THEME_BASE_ROLES 6U

typedef enum {
    UI_THEME_STANDARD = 0,
    UI_THEME_CONTRAST,
    UI_THEME_PRESET_COUNT,
} ui_theme_preset_t;

/* 0xRRGGBB per role. */
typedef struct {
    uint32_t rgb[UI_ROLE_COUNT];
} ui_palette_t;

/* A preset past the end reads as the standard one. */
void ui_theme_preset(ui_theme_preset_t preset, ui_palette_t *palette);
/* The six base colours, in role order, and everything derived from them. */
void ui_theme_derive(const uint32_t base[UI_THEME_BASE_ROLES], ui_palette_t *palette);

/* `weight` of 256 is all of `to`, 0 all of `from`; per channel, rounded. */
uint32_t ui_theme_mix(uint32_t from, uint32_t to, unsigned weight);

/* "#RRGGBB" or "RRGGBB", either case; nothing else. */
bool ui_theme_parse_hex(const char *text, uint32_t *rgb);
/* "#RRGGBB" into at least 8 bytes. */
void ui_theme_format_hex(uint32_t rgb, char *out, size_t out_size);
