#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ui_theme.h"

/* The values ui.c carried as constants before themes. The standard theme is
 * the promise that choosing nothing changes nothing, so it is pinned here
 * colour by colour rather than trusted. */
static void test_the_standard_theme_is_the_old_panel(void)
{
    ui_palette_t p;
    memset(&p, 0xAA, sizeof(p));
    ui_theme_preset(UI_THEME_STANDARD, &p);
    assert(p.rgb[UI_ROLE_GROUND] == 0x101820);
    assert(p.rgb[UI_ROLE_TEXT] == 0xFFFFFF);
    assert(p.rgb[UI_ROLE_ARTIST] == 0xB0BEC5);
    assert(p.rgb[UI_ROLE_SECONDARY] == 0xB0BEC5);
    assert(p.rgb[UI_ROLE_DIM] == 0x78909C);
    assert(p.rgb[UI_ROLE_ACCENT] == 0xF2A33C);
    assert(p.rgb[UI_ROLE_STRIP] == 0x1E2C3A);
    assert(p.rgb[UI_ROLE_TILE] == 0x18242E);
    assert(p.rgb[UI_ROLE_TILE_EDGE] == 0x26343F);
    assert(p.rgb[UI_ROLE_RULE] == 0x334454);
    assert(p.rgb[UI_ROLE_FIELD_ROW] == 0x1D2A36);
    assert(p.rgb[UI_ROLE_SELECTED] == 0x2A3B4A);
    assert(p.rgb[UI_ROLE_CURSOR] == 0x3F6187);
    assert(p.rgb[UI_ROLE_DISABLED] == 0x4E606C);
    assert(p.rgb[UI_ROLE_TRACK] == 0x23303C);
    assert(p.rgb[UI_ROLE_BAR_OFF] == 0x2D3F4D);
    assert(p.rgb[UI_ROLE_VU_OFF] == 0x263746);
    assert(p.rgb[UI_ROLE_FM_UNLIT] == 0x37474F);
    assert(p.rgb[UI_ROLE_ART_NOTE] == 0x3E5060);
    assert(p.rgb[UI_ROLE_FEED_NEAR] == 0x8FA8BC);
    assert(p.rgb[UI_ROLE_FEED_FAR] == 0x46586A);
    assert(p.rgb[UI_ROLE_FEED_DOT] == 0x33445A);
    // A preset past the end is the standard one, not garbage.
    ui_palette_t q;
    ui_theme_preset((ui_theme_preset_t)99, &q);
    assert(memcmp(&p, &q, sizeof(p)) == 0);
}

static void test_mix_reaches_both_ends_and_rounds(void)
{
    assert(ui_theme_mix(0x102030, 0xF0E0D0, 0U) == 0x102030);
    assert(ui_theme_mix(0x102030, 0xF0E0D0, 256U) == 0xF0E0D0);
    assert(ui_theme_mix(0x000000, 0xFFFFFF, 128U) == 0x808080);
    assert(ui_theme_mix(0x000000, 0xFFFFFF, 999U) == 0xFFFFFF);
    // Channels do not bleed into each other.
    assert(ui_theme_mix(0xFF0000, 0x00FF00, 256U) == 0x00FF00);
}

static unsigned luma(uint32_t rgb)
{
    return 2U * ((rgb >> 16) & 0xFFU) + 5U * ((rgb >> 8) & 0xFFU) + (rgb & 0xFFU);
}

/* The surfaces keep the standard theme's order away from the ground - a tile
 * below a strip below a rule, the cursor clearly above the selected row - on a
 * dark ground and on a light one alike. */
static void test_derived_surfaces_keep_their_order(void)
{
    const uint32_t dark[UI_THEME_BASE_ROLES] = {0x000000, 0xFFFFFF, 0xE8E8E8,
                                                0xE0E6EA, 0xB0BEC5, 0xFFB74D};
    const uint32_t light[UI_THEME_BASE_ROLES] = {0xFFFFFF, 0x000000, 0x202020,
                                                 0x303030, 0x606060, 0xC04000};
    for (int pass = 0; pass < 2; ++pass) {
        ui_palette_t p;
        ui_theme_derive(pass == 0 ? dark : light, &p);
        const uint32_t ground = p.rgb[UI_ROLE_GROUND];
        const unsigned g = luma(ground);
#define DIST(role) (luma(p.rgb[role]) > g ? luma(p.rgb[role]) - g : g - luma(p.rgb[role]))
        assert(DIST(UI_ROLE_TILE) < DIST(UI_ROLE_STRIP));
        assert(DIST(UI_ROLE_STRIP) < DIST(UI_ROLE_RULE));
        assert(DIST(UI_ROLE_FIELD_ROW) < DIST(UI_ROLE_SELECTED));
        assert(DIST(UI_ROLE_SELECTED) < DIST(UI_ROLE_CURSOR));
        assert(DIST(UI_ROLE_FEED_FAR) < DIST(UI_ROLE_FEED_NEAR));
        assert(DIST(UI_ROLE_CURSOR) < DIST(UI_ROLE_TEXT));
#undef DIST
        // The six chosen colours come through untouched.
        const uint32_t *base = pass == 0 ? dark : light;
        for (unsigned role = 0U; role < UI_THEME_BASE_ROLES; ++role) {
            assert(p.rgb[role] == base[role]);
        }
    }
}

static void test_the_contrast_theme_is_black_and_bright(void)
{
    ui_palette_t p;
    ui_theme_preset(UI_THEME_CONTRAST, &p);
    assert(p.rgb[UI_ROLE_GROUND] == 0x000000);
    assert(p.rgb[UI_ROLE_TEXT] == 0xFFFFFF);
    assert(p.rgb[UI_ROLE_ARTIST] == 0xE8E8E8);
    assert(p.rgb[UI_ROLE_SECONDARY] == 0xE0E6EA);
    assert(p.rgb[UI_ROLE_DIM] == 0xB0BEC5);
    assert(p.rgb[UI_ROLE_ACCENT] == 0xFFB74D);
    // Its dim text is brighter than the standard theme's secondary text.
    ui_palette_t standard;
    ui_theme_preset(UI_THEME_STANDARD, &standard);
    assert(luma(p.rgb[UI_ROLE_DIM]) >= luma(standard.rgb[UI_ROLE_SECONDARY]));
}

static void test_hex_round_trips_and_refuses_the_rest(void)
{
    uint32_t rgb = 0U;
    assert(ui_theme_parse_hex("#1a2B3c", &rgb) && rgb == 0x1A2B3C);
    assert(ui_theme_parse_hex("FFB74D", &rgb) && rgb == 0xFFB74D);
    const char *bad[] = {"", "#", "#12345", "#1234567", "12 456", "#GG0000", "0x123456", "##123456"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        rgb = 0xDEADU;
        assert(!ui_theme_parse_hex(bad[i], &rgb));
        assert(rgb == 0xDEADU);
    }
    assert(!ui_theme_parse_hex(NULL, &rgb));
    char text[8];
    ui_theme_format_hex(0x0A0B0C, text, sizeof(text));
    assert(strcmp(text, "#0A0B0C") == 0);
    assert(ui_theme_parse_hex(text, &rgb) && rgb == 0x0A0B0C);
}

int main(void)
{
    test_the_standard_theme_is_the_old_panel();
    test_mix_reaches_both_ends_and_rounds();
    test_derived_surfaces_keep_their_order();
    test_the_contrast_theme_is_black_and_bright();
    test_hex_round_trips_and_refuses_the_rest();
    puts("ui_theme tests passed");
    return 0;
}
