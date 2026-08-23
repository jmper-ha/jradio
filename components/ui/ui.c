#include "ui.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "system_report.h"

#include "board.h"
#include "board_display_profile.h"
#include "device_settings.h"
#include "player_control.h"
#include "wifi_provisioning.h"
#include "ui_autoplay.h"
#include "ui_click_gesture.h"
#include "ui_draw_buffer.h"
#include "ui_font_cyrillic_14.h"
#include "ui_font_cyrillic_20.h"
#include "ui_menu.h"
#include "ui_feed_icon_bitmaps.h"
#include "ui_feed_icons.h"
#include "ui_feed_model.h"
#include "ui_player_state.h"
#include "ui_radio_text.h"
#include "ui_seek.h"
#include "ui_settings_model.h"
#include "ui_station_list.h"
#include "ui_status_bar.h"
#include "ui_web_address.h"
#include "file_track_progress.h"

#include "audio_volume.h"
#include "device_clock.h"
#include "ui_files_notice.h"
#include "album_art.h"
#include "ui_vu_meter.h"

#define UI_DRAW_BUFFER_LINES 20
#define UI_DRAW_BUFFER_SIZE ui_rgb565_draw_buffer_size(TFT_WIDTH, UI_DRAW_BUFFER_LINES)
#define UI_INPUT_QUEUE_LENGTH 16
/* Measured, not guessed: at 6144 the periodic health report found this task
 * down to 504 bytes of headroom - LVGL's rendering plus a logging call, with
 * an interrupt frame able to land on top at any moment. The peak draw is
 * around 5.6 KB, so this leaves roughly 2.5 KB of margin. */
#define UI_TASK_STACK_SIZE 8192
#define UI_TASK_PRIORITY 4
/* Five rows of 20 px text. The count follows from the size: at 14 px six rows
 * fitted, but a name read from across the room did not, and the row pitch a
 * legible face needs leaves room for five above the scroll bar. */
#define UI_STATION_LIST_MAX_ROWS 5U

/* The screens share one palette, stated here rather than repeated as literals.
 * The player screen defined it by accretion; the others were on a different
 * set entirely - white titles and a blue selection against the same ground -
 * and the only way "the same style" survives the next edit is if there is one
 * place to change. */
#define UI_COLOR_GROUND 0x101820
#define UI_COLOR_STRIP 0x1E2C3A
#define UI_COLOR_TILE 0x18242E
#define UI_COLOR_TILE_EDGE 0x26343F
#define UI_COLOR_RULE 0x334454
#define UI_COLOR_ACCENT 0xF2A33C
#define UI_COLOR_TEXT 0xFFFFFF
#define UI_COLOR_MUTED 0xB0BEC5
#define UI_COLOR_DIM 0x78909C
/* Only ever a warning; never decoration, so it stays out of the ramp above. */
#define UI_COLOR_NOTICE 0xFFD54F
/* Folder rows in the USB browser. Deliberately duller than the accent, which
 * means "this is the one playing" - a folder is a place, not a state. */
#define UI_COLOR_FOLDER 0xC08A1E
/* A raised step rather than a colour of its own: the row under the cursor is
 * lifted off the ground and its text takes the accent, which is how the player
 * screen marks the thing being played. */
#define UI_COLOR_SELECTED 0x2A3B4A

/* Height of the status strip every screen carries. */
#define UI_STRIP_H 26

/* Home screen carousel. Every icon is a 24x24 design scaled to one of three
 * sizes, so a single axis is enough - the old row needed a per-glyph vertical
 * inset because each FontAwesome symbol had its own height, and the inset only
 * ever fitted one of them.
 *
 * The five centres are symmetric about 160 with 16 px of margin at both ends;
 * the row used to start 4 px from the left and 30 from the right, which read
 * as the whole thing sliding off the screen. */
#define UI_FEED_AXIS_Y 138
#define UI_FEED_CENTER_X 160
#define UI_FEED_INNER_DX 68
#define UI_FEED_OUTER_DX 132
#define UI_FEED_TILE 84
#define UI_FEED_ICON_LARGE_PX 48
#define UI_FEED_ICON_MEDIUM_PX 32
#define UI_FEED_ICON_SMALL_PX 24
/* Depth is carried by brightness as well as size: without it the middle icon
 * reads as the only lit one rather than as the middle of a ring. */
#define UI_COLOR_FEED_NEAR 0x8FA8BC
#define UI_COLOR_FEED_FAR 0x46586A
/* Settings screen. Group headings carry the 20 px face and the fields under
 * them stay at 14, so the three groups read as the structure of the screen
 * rather than as five rows of equal weight. The pitch has to clear the taller
 * heading; at most five rows are ever visible, since only one group is open. */
#define UI_SET_ROW_Y 40
#define UI_SET_ROW_PITCH 28
#define UI_SET_ROW_H 26
/* The band along the bottom is the only place the device says how to reach its
 * web UI. It sits below everything else and never scrolls away. */
#define UI_SET_BAND_Y 210
#define UI_SET_BAND_H 30

/* The other home screen: the same items as a list. Eight rows of 24 px start
 * right under the strip and end at 222, which is every pixel the screen has -
 * the previous 27 px pitch fitted seven items and ran off the bottom as soon
 * as the SD card row appeared. The icon column is the carousel's 24 px bitmap,
 * so both home screens name an item the same way. */
#define UI_MENU_ROW_Y (UI_STRIP_H + 4)
#define UI_MENU_ROW_PITCH 24
#define UI_MENU_ROW_H 24
#define UI_MENU_ROW_X 6
#define UI_MENU_ROW_W 308
#define UI_MENU_ICON_X 12
#define UI_MENU_TEXT_PAD 38

/* One dot per item, so a ring of eight does not feel endless. */
#define UI_FEED_DOT 6
#define UI_FEED_DOT_PITCH 10
#define UI_FEED_DOT_Y 200
#define UI_COLOR_FEED_DOT 0x33445A

/* List screens: rows start straight under the strip, and a rule and the
 * position bar close the screen the way they close the player's. */
#define UI_LIST_ROW_Y (UI_STRIP_H + 6)
#define UI_LIST_ROW_PITCH 36
#define UI_LIST_ROW_H 30
/* Width the folder glyph reserves at the left of a row: 24 px of Montserrat 24
 * (its adv_w) plus the gap before the name. */
#define UI_LIST_ICON_W 30
#define UI_LIST_RULE_Y (UI_LIST_ROW_Y + (int)UI_STATION_LIST_MAX_ROWS * UI_LIST_ROW_PITCH + 6)
#define UI_LIST_PROGRESS_Y (UI_LIST_RULE_Y + 8)
/* The empty-browser screen: a drive above the sentence that says what is wrong
 * with it, together in the middle of the space the rows leave behind. */
#define UI_LIST_NOTICE_ICON 48
#define UI_LIST_NOTICE_ICON_Y 84
#define UI_LIST_NOTICE_TEXT_Y 148

#define UI_RADIO_EMPTY_LIST_DELAY_MS 250U
#define UI_STATION_LIST_IDLE_TIMEOUT_MS 10000U
#define UI_SETTINGS_MAX_ROWS 7U

static const char *TAG = "ui";
static QueueHandle_t s_input_queue;
static ui_menu_state_t s_menu;
static lv_display_t *s_display;
static lv_obj_t *s_menu_screen;
static lv_obj_t *s_source_screen;
static lv_obj_t *s_menu_rows[UI_MENU_ITEM_COUNT];
static lv_obj_t *s_menu_icons[UI_MENU_ITEM_COUNT];
static lv_obj_t *s_menu_notice;
static lv_obj_t *s_feed_screen;
static lv_obj_t *s_feed_title;
static lv_obj_t *s_feed_notice;
static lv_obj_t *s_feed_icons[5];
static lv_obj_t *s_feed_dots[UI_FEED_ITEM_COUNT];
static ui_feed_model_t s_feed_model;
static lv_obj_t *s_source_title;
static lv_obj_t *s_source_status;
static lv_obj_t *s_source_detail;
static lv_obj_t *s_source_stream;
static lv_obj_t *s_source_buffer;
static lv_obj_t *s_source_artist;
/* Every screen carries the same strip, so it is built once and each screen
 * gets its own instance - LVGL objects belong to one parent, so they cannot be
 * moved between screens. Only the active screen's is refreshed. */
/* Four rising bars. Drawn rather than taken from a font: LVGL's symbol set has
 * one Wi-Fi glyph with no strength in it, and the strength is the half worth
 * showing. */
#define UI_WIFI_BARS 4
#define UI_COLOR_BAR_OFF 0x2D3F4D

typedef struct {
    lv_obj_t *context;
    lv_obj_t *clock;
    lv_obj_t *rssi;
    lv_obj_t *bars[UI_WIFI_BARS];
    /* Per strip, not shared: only the active screen's is refreshed, so a
     * shared cache would go stale the moment the screen changed and leave the
     * new strip painted at whatever it was built with. */
    uint32_t bar_colour[UI_WIFI_BARS];
} ui_status_strip_t;

static ui_status_strip_t s_source_strip;
static ui_status_strip_t s_menu_strip;
static ui_status_strip_t s_feed_strip;
static ui_status_strip_t s_list_strip;
static lv_obj_t *s_source_art;
/* The cover sits on top of the placeholder tile rather than replacing it. The
 * two change over often - every track, and back to nothing the moment the
 * radio is selected - and showing or hiding one object is steadier than
 * rebuilding the other. */
static lv_obj_t *s_source_cover;
static lv_image_dsc_t s_source_cover_dsc;
/* The screen's own copy of the pixels, so LVGL can draw from them whenever it
 * likes without the playback task writing underneath it. */
static uint16_t *s_source_cover_pixels;
static unsigned int s_source_cover_generation;
static lv_obj_t *s_source_volume;
static lv_obj_t *s_source_volume_bar;
static lv_obj_t *s_source_volume_icon;
static lv_obj_t *s_source_pause;
static lv_obj_t *s_source_progress;
/* Four rising bars. Drawn rather than taken from a font: LVGL's symbol set has
 * one Wi-Fi glyph with no strength in it, and the strength is the half worth
 * showing. */
/* One encoder click. Five percent is 2 dB on this curve - a step you can hear
 * without hunting, and 20 clicks from silence to full. */
#define UI_VOLUME_STEP_PERCENT 5
/* How long the knob has to be still before the setting is written to flash.
 * Long enough to cover a whole gesture, short enough that a power cut right
 * after adjusting is unlikely to lose it. */
#define UI_VOLUME_SETTLE_MS 1500U
/* 20 blocks per channel: 20*11 + 19*3 = 277 px starting at x=30, so the row
 * ends at 307 on a 320 px panel. */
#define UI_VU_SEGMENTS 20U
#define UI_VU_SEGMENT_W 11
#define UI_VU_SEGMENT_GAP 3
static lv_obj_t *s_source_vu[2][UI_VU_SEGMENTS];
/* Colour last written to each block, so an unchanged one is left alone; see
 * ui_update_vu() for why that matters so much. 0 is not a colour any block
 * takes, so the first pass always paints. */
static uint32_t s_vu_colour[2][UI_VU_SEGMENTS];
static ui_vu_meter_t s_vu_state[2];
static uint32_t s_vu_updated_ms;
static lv_obj_t *s_settings_screen;
static lv_obj_t *s_settings_rows[UI_SETTINGS_MAX_ROWS];
#define UI_SETTINGS_SWITCH_COUNT 3U
static lv_obj_t *s_settings_switches[UI_SETTINGS_SWITCH_COUNT];
static lv_obj_t *s_settings_web_band;
static lv_obj_t *s_settings_web_address;
static lv_obj_t *s_settings_notice;
static ui_settings_model_t s_settings_model;
static device_settings_t s_device_settings;
static bool s_settings_open;
static lv_obj_t *s_station_list_screen;
static lv_obj_t *s_station_list_title;
static lv_obj_t *s_station_list_notice;
/* The notice replaces the list rather than sitting above it, so the two parts
 * that only make sense with rows behind them - the rule and the drive picture
 * that stands in for them - are held to be shown and hidden together with it. */
static lv_obj_t *s_station_list_notice_icon;
static lv_obj_t *s_station_list_rule;
static lv_obj_t *s_station_list_rows[UI_STATION_LIST_MAX_ROWS];
/* The folder mark is a label of its own rather than a glyph inside the row's
 * text: inline it would take the row's font and colour, and it has to be
 * bigger than the name beside it and in its own colour. */
static lv_obj_t *s_station_list_icons[UI_STATION_LIST_MAX_ROWS];
static lv_obj_t *s_station_list_progress;
static station_list_state_t s_station_list;
static ui_player_state_t s_player_ui;
// Only the player screen tells a single click from a double one, so only
// there does a click wait out the window; every other screen acts at once.
static ui_click_gesture_t s_player_click;
/* Open only while the encoder is scrubbing the playing file. It takes over
 * both the knob and the progress bar, so everything that leaves the player
 * screen has to close it - a thick bar left behind would be a mode with no way
 * back into it. */
static ui_seek_t s_player_seek;
static bool s_waiting_for_radio_station;
static uint32_t s_radio_station_wait_started_ms;
// The USB browser reuses this list screen. Outside the drive's root it shows a
// ".." row above the entries, so every listing index is one below its row.
static bool s_file_browser_has_parent_row;
static unsigned int s_files_listing_revision;
// The USB source screen has nothing to show until a file is picked, so
// selecting the source jumps straight to the browser. The jump waits for the
// listing revision to move, otherwise the list would open on the previous
// directory's rows for a poll or two.
// The USB screen can be opened with no usable drive: it then shows the reason
// instead of rows. Kept from the last snapshot because ui_show_source() runs
// on a key press, not on the poll that carries the snapshot.
static bool s_files_unavailable;
// One per volume, as the snapshot last reported them, so the menu can answer
// "can this source be opened" for whichever row the cursor is on.
static file_browser_media_t s_last_usb_media = FILE_BROWSER_MEDIA_ABSENT;
static file_browser_media_t s_last_sd_media = FILE_BROWSER_MEDIA_ABSENT;
static size_t s_last_files_entry_count;
/* Which source the "nothing to browse" screen is explaining. Without it the
 * notice would name a USB drive while the user was asking for the card, and
 * the pick-up below would select the wrong source when a volume appeared. */
static audio_source_t s_files_unavailable_source = AUDIO_SOURCE_NONE;
// Autoplay runs once, and only after the drive has had time to appear: USB
// mounts around five seconds in, later still when the root port needs
// re-enumerating, so deciding "no drive" any earlier would be deciding it
// before the answer exists.
#define UI_AUTOPLAY_FILE_WAIT_MS 12000U
static bool s_autoplay_pending;
static uint32_t s_autoplay_started_ms;
static bool s_files_list_open_requested;
static unsigned int s_files_list_open_revision;

static bool ui_list_shows_files(void)
{
    return audio_source_is_files(ui_player_state_source(&s_player_ui));
}

static file_browser_media_t ui_media_for_source(audio_source_t source)
{
    return source == AUDIO_SOURCE_SD ? s_last_sd_media : s_last_usb_media;
}

/* The icon that goes with a source, for the screen that explains why its
 * browser is empty. Asked of the feed's own mapping rather than kept as a
 * second table: a card drawn as a flash drive sends the user to the wrong
 * socket, which is the same mistake the notice text exists to avoid. */
static const lv_image_dsc_t *ui_source_icon(audio_source_t source)
{
    ui_feed_model_t model;
    ui_feed_model_init(&model, 0U);
    if (!ui_feed_model_select_source(&model, source)) return NULL;
    return ui_feed_icon_image(ui_feed_model_selected(&model), UI_FEED_ICON_LARGE);
}

// Short name for the strip and the list title.
static const char *ui_source_short_name(audio_source_t source)
{
    return source == AUDIO_SOURCE_SD ? "SD" : "USB";
}

static size_t ui_files_row_offset(void)
{
    return s_file_browser_has_parent_row ? 1U : 0U;
}

static uint32_t ui_tick_get_ms(void);
/* Set when the volume has moved but not yet been saved; see
 * ui_volume_commit_due() for why the two are separate. */
static bool s_volume_save_pending;
static uint32_t s_volume_changed_ms;

static void ui_set_label_text_if_changed(lv_obj_t *label, const char *text)
{
    if (label == NULL || text == NULL) {
        return;
    }
    const char *current = lv_label_get_text(label);
    if (current == NULL || strcmp(current, text) != 0) {
        lv_label_set_text(label, text);
    }
}

static const char *ui_radio_state_text(player_playback_state_t state)
{
    switch (state) {
    case PLAYER_PLAYBACK_CONNECTING: return "Connecting...";
    case PLAYER_PLAYBACK_PLAYING: return "Playing";
    case PLAYER_PLAYBACK_PAUSED: return "Paused";
    case PLAYER_PLAYBACK_RECONNECTING: return "Reconnecting...";
    case PLAYER_PLAYBACK_ERROR: return "Connection error";
    case PLAYER_PLAYBACK_STOPPED: return "Stopped";
    }
    return "Unknown";
}

/* Builds the strip on `screen`. Left slot names the screen, centre carries the
 * clock and the right the signal - the same three positions everywhere, so
 * moving between screens does not move the eye. */
static void ui_status_strip_create(lv_obj_t *screen, ui_status_strip_t *strip,
                                   const char *context)
{
    lv_obj_t *band = lv_obj_create(screen);
    lv_obj_set_pos(band, 0, 0);
    lv_obj_set_size(band, 320, UI_STRIP_H);
    lv_obj_set_style_bg_color(band, lv_color_hex(UI_COLOR_STRIP), 0);
    lv_obj_set_style_border_width(band, 0, 0);
    lv_obj_set_style_radius(band, 0, 0);
    lv_obj_set_style_pad_all(band, 0, 0);
    lv_obj_clear_flag(band, LV_OBJ_FLAG_SCROLLABLE);

    strip->context = lv_label_create(screen);
    lv_obj_set_pos(strip->context, 10, 6);
    lv_obj_set_size(strip->context, 116, 19);
    lv_label_set_long_mode(strip->context, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(strip->context, lv_color_hex(UI_COLOR_ACCENT), 0);
    lv_label_set_text(strip->context, context);

    // Centred rather than left-aligned: it is the one thing on the screen read
    // from across the room, and the middle is where the eye goes first.
    strip->clock = lv_label_create(screen);
    lv_obj_set_pos(strip->clock, 130, 5);
    lv_obj_set_width(strip->clock, 60);
    lv_obj_set_style_text_align(strip->clock, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(strip->clock, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_label_set_text(strip->clock, "");

    for (int bar = 0; bar < UI_WIFI_BARS; ++bar) {
        lv_obj_t *block = lv_obj_create(screen);
        const int height = 3 + bar * 3;
        lv_obj_set_size(block, 3, height);
        // Grown from a common baseline so the four read as one rising shape.
        lv_obj_set_pos(block, 252 + bar * 5, 7 + (12 - height));
        lv_obj_set_style_border_width(block, 0, 0);
        lv_obj_set_style_radius(block, 1, 0);
        lv_obj_set_style_pad_all(block, 0, 0);
        lv_obj_set_style_bg_color(block, lv_color_hex(UI_COLOR_BAR_OFF), 0);
        lv_obj_clear_flag(block, LV_OBJ_FLAG_SCROLLABLE);
        strip->bars[bar] = block;
        strip->bar_colour[bar] = UI_COLOR_BAR_OFF;
    }
    strip->rssi = lv_label_create(screen);
    lv_obj_set_pos(strip->rssi, 275, 6);
    lv_obj_set_style_text_color(strip->rssi, lv_color_hex(UI_COLOR_MUTED), 0);
    lv_label_set_text(strip->rssi, "");
}

static void ui_status_strip_update(ui_status_strip_t *strip,
                                   const player_snapshot_t *snapshot)
{
    if (strip == NULL || strip->clock == NULL) return;

    int hour = 0;
    int minute = 0;
    const bool have_time = device_clock_now(&hour, &minute);
    char clock_text[8];
    ui_status_clock_text(clock_text, sizeof(clock_text), have_time, hour, minute);
    ui_set_label_text_if_changed(strip->clock, clock_text);

    const uint8_t bars = ui_status_wifi_bars(snapshot->wifi_rssi_valid,
                                             snapshot->wifi_rssi_dbm);
    for (int bar = 0; bar < UI_WIFI_BARS; ++bar) {
        // Unlit bars stay visible in a dim shade rather than hiding, so the
        // shape reads as a scale at rest instead of a missing icon.
        const uint32_t colour = bar < (int)bars ? UI_COLOR_MUTED : UI_COLOR_BAR_OFF;
        if (strip->bar_colour[bar] == colour) continue;
        strip->bar_colour[bar] = colour;
        lv_obj_set_style_bg_color(strip->bars[bar], lv_color_hex(colour), 0);
    }
    char rssi_text[8];
    if (snapshot->wifi_rssi_valid) {
        snprintf(rssi_text, sizeof(rssi_text), "%d", (int)snapshot->wifi_rssi_dbm);
    } else {
        snprintf(rssi_text, sizeof(rssi_text), "--");
    }
    ui_set_label_text_if_changed(strip->rssi, rssi_text);
}

/* Runs every poll rather than only on a snapshot change: both readings here
 * come straight from the owning subsystem and move far too often to belong in
 * a structure the web diffs against. */
static void ui_update_footer(void)
{
    if (lv_screen_active() != s_source_screen) return;

    /* A file has a position, a stream does not, so the left of the footer says
     * different things for each. Where a track is matters more than how full
     * the buffer is - reading a file, the buffer sits at the brim and never
     * moves. */
    uint32_t elapsed = 0U;
    uint32_t total = 0U;
    // Two hour-long times and a separator: 12 + 3 + 12 and room to spare, so
    // the compiler can see it never truncates.
    char left_text[32];
    uint8_t played_percent = 0U;
    const bool have_track = player_control_track_progress(&elapsed, &total);
    bool have_bar = have_track && file_track_progress_percent(elapsed, total,
                                                             &played_percent);
    if (ui_seek_is_active(&s_player_seek)) {
        /* Both readouts follow the knob rather than the file while the mode
         * is open: the bar is being aimed, not reported, and the number
         * beside it is how the place being aimed at is read exactly. The
         * track plays on underneath, a second or two away from either. */
        elapsed = ui_seek_target(&s_player_seek);
        played_percent = ui_seek_percent(&s_player_seek);
        have_bar = true;
    }
    if (have_track) {
        char elapsed_text[12];
        file_track_time_text(elapsed_text, sizeof(elapsed_text), elapsed);
        if (total > 0U) {
            char total_text[12];
            file_track_time_text(total_text, sizeof(total_text), total);
            snprintf(left_text, sizeof(left_text), "%s / %s", elapsed_text, total_text);
        } else {
            // The length is not known until the first frame is decoded; the
            // position already is, and is worth showing on its own.
            snprintf(left_text, sizeof(left_text), "%s", elapsed_text);
        }
    } else if (audio_source_is_files(ui_player_state_source(&s_player_ui))) {
        /* A file's buffer sits at the brim from the first block, so the
         * reading says nothing; and until the decoder has found its first
         * frame there is no position to show either. The slot stays empty
         * rather than opening the screen with a number about nothing. */
        left_text[0] = '\0';
    } else {
        uint8_t fill = 0U;
        if (player_control_input_fill(&fill)) {
            snprintf(left_text, sizeof(left_text), "Буфер %u%%", (unsigned int)fill);
        } else {
            // Nothing playing, or a source with no backlog at all. A dash says
            // that; a zero would claim the buffer had run dry.
            snprintf(left_text, sizeof(left_text), "Буфер --");
        }
    }
    ui_set_label_text_if_changed(s_source_buffer, left_text);

    if (have_bar) {
        lv_obj_clear_flag(s_source_progress, LV_OBJ_FLAG_HIDDEN);
        lv_obj_t *played = lv_obj_get_child(s_source_progress, 0);
        if (played != NULL) {
            const int32_t width = (int32_t)((300U * played_percent) / 100U);
            const int32_t clamped = width < 1 ? 1 : width;
            // Written only on a change, like every other value in this loop:
            // a resize invalidates, and this runs every pass.
            if (lv_obj_get_width(played) != clamped) lv_obj_set_width(played, clamped);
        }
    } else {
        lv_obj_add_flag(s_source_progress, LV_OBJ_FLAG_HIDDEN);
    }

    const uint8_t volume = board_audio_volume();
    char volume_text[8];
    snprintf(volume_text, sizeof(volume_text), "%u", (unsigned int)volume);
    ui_set_label_text_if_changed(s_source_volume, volume_text);
    lv_obj_t *fill_bar = lv_obj_get_child(s_source_volume_bar, 0);
    if (fill_bar != NULL) {
        const int32_t width = (int32_t)((60U * volume) / 100U);
        const int32_t clamped = width < 1 ? 1 : width;
        // Same reason as the meter blocks: resizing invalidates, and this runs
        // every pass while the volume changes only when the knob turns.
        if (lv_obj_get_width(fill_bar) != clamped) lv_obj_set_width(fill_bar, clamped);
    }
}

/* The badge and the state line answer the same question, so they are decided
 * together: whichever is showing, the other is not, and the performer row is
 * only the performer's when neither wants it. */
static void ui_update_playback_marks(const player_snapshot_t *snapshot)
{
    const bool paused = snapshot->playback_state == PLAYER_PLAYBACK_PAUSED;
    if (paused) {
        lv_obj_clear_flag(s_source_pause, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_source_pause, LV_OBJ_FLAG_HIDDEN);
    }
}

/* Puts `state` on the performer row, or gives the row back to the performer.
 *
 * Hiding one of the two is what makes this safe. They share a row because
 * there is no third line to spare, and an earlier version relied on the
 * performer being empty whenever a state was worth showing - which is false on
 * pause, where the title is still there and the two drew on top of each
 * other. */
static void ui_set_state_line(const char *state, const char *artist)
{
    const bool show_state = state != NULL && state[0] != '\0';
    ui_set_label_text_if_changed(s_source_status, show_state ? state : "");
    ui_set_label_text_if_changed(s_source_artist, show_state ? "" : artist);
    if (show_state) {
        lv_obj_add_flag(s_source_artist, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_source_status, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_source_status, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_source_artist, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ui_update_files_status(const player_snapshot_t *snapshot)
{
    audio_tags_t tags;
    const bool tagged = player_control_track_tags(&tags);
    /* Each row falls back on its own: a file can name its album and not its
     * performer, and half a set of tags is still better than none. The album
     * takes the place the radio gives the station name, and the directory
     * stands in where the file did not name one. */
    ui_set_label_text_if_changed(s_source_title,
                                 tagged && tags.album[0] != '\0' ? tags.album
                                                                 : snapshot->context);
    ui_set_label_text_if_changed(s_source_detail, tagged && tags.title[0] != '\0'
                                                      ? tags.title
                                                      : snapshot->stream_title);
    // The performer row is the state line's whenever there is a state worth
    // naming. Pause is not one - the badge says it.
    ui_set_state_line(snapshot->playback_state == PLAYER_PLAYBACK_STOPPED ? "Выберите файл" : "",
                      tagged ? tags.artist : "");
    char stream_text[64];
    ui_radio_stream_text(stream_text, sizeof(stream_text), snapshot->codec,
                         snapshot->bitrate_kbps, snapshot->sample_rate_hz);
    ui_set_label_text_if_changed(s_source_stream, stream_text);
}

static void ui_update_radio_status(const player_snapshot_t *snapshot)
{
    if (snapshot == NULL) return;
    ui_update_playback_marks(snapshot);
    if (audio_source_is_files(ui_player_state_source(&s_player_ui))) {
        ui_update_files_status(snapshot);
        return;
    }
    if (ui_player_state_source(&s_player_ui) != AUDIO_SOURCE_INTERNET_RADIO) return;

    // While a station switch is pending confirmation, snapshot->active_item_index
    // still reflects the previous station; keep showing the one the user just
    // picked instead of flipping back to the old one for the pending window.
    size_t pending_item_index;
    const bool pending =
        ui_player_state_pending_item(&s_player_ui, &pending_item_index);
    const size_t display_item_index =
        pending ? pending_item_index : snapshot->active_item_index;
    const station_catalog_entry_t *entry =
        player_control_station_at(display_item_index);
    if (entry != NULL) {
        /* The stream's own name is only about the station the snapshot
         * describes, so while a switch is pending it names the previous one.
         * The list is the only source that can answer for the station just
         * picked. */
        const char *stream_name = pending ? "" : snapshot->context;
        ui_set_label_text_if_changed(
            s_source_title,
            ui_radio_station_name(entry->flag != 0, entry->name, stream_name));
    }

    // Whatever the flag says about the name, the track is the stream's to
    // tell; with nothing sent, the line stays empty rather than repeating the
    // station.
    const char *icy = snapshot->stream_title;
    // Stations send one string; splitting it gives the track its own wide line
    // instead of burying it in the middle of a run-on.
    char artist[PLAYER_TITLE_MAX_LEN];
    char track[PLAYER_TITLE_MAX_LEN];
    (void)ui_radio_split_title(icy, artist, sizeof(artist), track, sizeof(track));
    ui_set_label_text_if_changed(s_source_detail, track);

    // Playing and paused both leave the row to the performer: one needs no
    // announcement, the other has the badge. Connecting, reconnecting and
    // failure are the states worth a line.
    const bool settled = snapshot->playback_state == PLAYER_PLAYBACK_PLAYING ||
                         snapshot->playback_state == PLAYER_PLAYBACK_PAUSED;
    ui_set_state_line(settled ? "" : ui_radio_state_text(snapshot->playback_state), artist);

    char stream_text[64];
    ui_radio_stream_text(stream_text, sizeof(stream_text), snapshot->codec,
                         snapshot->bitrate_kbps, snapshot->sample_rate_hz);
    ui_set_label_text_if_changed(s_source_stream, stream_text);
}

static uint32_t ui_tick_get_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static const char *ui_feed_item_title(ui_feed_item_t item)
{
    return ui_menu_item_label((ui_menu_item_t)item);
}

static void ui_update_feed_screen(void)
{
    const ui_feed_item_t selected = ui_feed_model_selected(&s_feed_model);
    const int offsets[5] = {-2, -1, 0, 1, 2};
    const int centers[5] = {
        UI_FEED_CENTER_X - UI_FEED_OUTER_DX, UI_FEED_CENTER_X - UI_FEED_INNER_DX,
        UI_FEED_CENTER_X,
        UI_FEED_CENTER_X + UI_FEED_INNER_DX, UI_FEED_CENTER_X + UI_FEED_OUTER_DX,
    };
    const ui_feed_icon_size_t sizes[5] = {
        UI_FEED_ICON_SMALL, UI_FEED_ICON_MEDIUM, UI_FEED_ICON_LARGE,
        UI_FEED_ICON_MEDIUM, UI_FEED_ICON_SMALL,
    };
    const int pixels[5] = {
        UI_FEED_ICON_SMALL_PX, UI_FEED_ICON_MEDIUM_PX, UI_FEED_ICON_LARGE_PX,
        UI_FEED_ICON_MEDIUM_PX, UI_FEED_ICON_SMALL_PX,
    };
    const uint32_t colors[5] = {
        UI_COLOR_FEED_FAR, UI_COLOR_FEED_NEAR, UI_COLOR_ACCENT,
        UI_COLOR_FEED_NEAR, UI_COLOR_FEED_FAR,
    };
    for (size_t slot = 0; slot < 5U; ++slot) {
        int index = (int)selected + offsets[slot];
        while (index < 0) index += UI_FEED_ITEM_COUNT;
        index %= UI_FEED_ITEM_COUNT;
        const ui_feed_item_t item = (ui_feed_item_t)index;
        const bool center = slot == 2U;
        lv_image_set_src(s_feed_icons[slot], ui_feed_icon_image(item, sizes[slot]));
        /* The tile is bigger than the icon inside it, so the object is placed
         * by its own box and the image centred within it. Everything else is
         * exactly icon-sized. */
        const int box = center ? UI_FEED_TILE : pixels[slot];
        lv_obj_set_size(s_feed_icons[slot], box, box);
        lv_obj_set_pos(s_feed_icons[slot], centers[slot] - box / 2, UI_FEED_AXIS_Y - box / 2);
        /* An A8 bitmap has no colour of its own: LVGL blends it with the
         * recolour, which is what lets one image serve every slot. */
        lv_obj_set_style_image_recolor(s_feed_icons[slot], lv_color_hex(colors[slot]), 0);
        lv_obj_set_style_image_recolor_opa(s_feed_icons[slot], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_feed_icons[slot], center ? 2 : 0, 0);
        lv_obj_set_style_border_color(s_feed_icons[slot], lv_color_hex(UI_COLOR_ACCENT), 0);
        lv_obj_set_style_border_opa(s_feed_icons[slot], LV_OPA_COVER, 0);
        /* Only the tile is rounded: an lv_image clips its bitmap to the widget
         * radius, and 14 px on a 24 px neighbour would round the icon itself
         * into a circle. */
        lv_obj_set_style_radius(s_feed_icons[slot], center ? 14 : 0, 0);
        lv_obj_set_style_bg_color(s_feed_icons[slot], lv_color_hex(UI_COLOR_SELECTED), 0);
        lv_obj_set_style_bg_opa(s_feed_icons[slot], center ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
    for (uint8_t index = 0; index < UI_FEED_ITEM_COUNT; ++index) {
        lv_obj_set_style_bg_color(s_feed_dots[index],
                                  lv_color_hex(index == (uint8_t)selected ? UI_COLOR_ACCENT
                                                                          : UI_COLOR_FEED_DOT), 0);
    }
    lv_label_set_text(s_feed_title, ui_feed_item_title(selected));
}

static void ui_create_feed_screen(void)
{
    s_feed_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_feed_screen, lv_color_hex(UI_COLOR_GROUND), 0);
    lv_obj_set_style_border_width(s_feed_screen, 0, 0);
    lv_obj_set_style_pad_all(s_feed_screen, 0, 0);
    lv_obj_set_style_text_font(s_feed_screen, &ui_font_cyrillic_14, 0);
    ui_status_strip_create(s_feed_screen, &s_feed_strip, "jRadio");

    s_feed_title = lv_label_create(s_feed_screen);
    lv_obj_set_pos(s_feed_title, 0, UI_STRIP_H + 8);
    lv_obj_set_size(s_feed_title, 320, 28);
    lv_obj_set_style_text_align(s_feed_title, LV_TEXT_ALIGN_CENTER, 0);
    /* A real 20 px face, not the 14 px one scaled up.
     *
     * The scale was a workaround from before this build had a large Cyrillic
     * font, and it was the only transform in the whole interface. LVGL renders
     * a transformed object through an intermediate layer, and on this screen
     * that composite wedged lv_timer_handler() in a loop it never came out of:
     * the UI task pinned a core until the task watchdog fired, which looked
     * like the display freezing on the way back to the home screen. A drawn
     * glyph is also sharper than a stretched one. */
    lv_obj_set_style_text_font(s_feed_title, &ui_font_cyrillic_20, 0);
    lv_obj_set_style_text_color(s_feed_title, lv_color_hex(UI_COLOR_TEXT), 0);
    s_feed_notice = lv_label_create(s_feed_screen);
    lv_obj_set_pos(s_feed_notice, 8, 218);
    lv_obj_set_size(s_feed_notice, 304, 18);
    lv_obj_set_style_text_align(s_feed_notice, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_feed_notice, lv_color_hex(0xFFCC80), 0);
    lv_label_set_text(s_feed_notice, "");
    for (size_t index = 0; index < 5U; ++index) {
        s_feed_icons[index] = lv_image_create(s_feed_screen);
        lv_obj_set_style_pad_all(s_feed_icons[index], 0, 0);
        // Centres the bitmap inside the tile, which is larger than it.
        lv_image_set_inner_align(s_feed_icons[index], LV_IMAGE_ALIGN_CENTER);
    }
    /* Centred as a group: eight dots at a pitch of 10 span 74 px, the last one
     * being a dot rather than a gap. */
    const int dots_left = UI_FEED_CENTER_X -
                          (UI_FEED_ITEM_COUNT * UI_FEED_DOT_PITCH - UI_FEED_DOT) / 2;
    for (uint8_t index = 0; index < UI_FEED_ITEM_COUNT; ++index) {
        s_feed_dots[index] = lv_obj_create(s_feed_screen);
        lv_obj_remove_style_all(s_feed_dots[index]);
        lv_obj_set_size(s_feed_dots[index], UI_FEED_DOT, UI_FEED_DOT);
        lv_obj_set_pos(s_feed_dots[index], dots_left + index * UI_FEED_DOT_PITCH,
                       UI_FEED_DOT_Y);
        lv_obj_set_style_radius(s_feed_dots[index], UI_FEED_DOT / 2, 0);
        lv_obj_set_style_bg_opa(s_feed_dots[index], LV_OPA_COVER, 0);
    }
    ui_feed_model_init(&s_feed_model, 0U);
    ui_update_feed_screen();
}

static void ui_flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    const esp_err_t err = board_display_draw_rgb565(area->x1, area->y1, area->x2 + 1, area->y2 + 1,
                                                     (const uint16_t *)pixels);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "display flush failed: %s", esp_err_to_name(err));
    }
    lv_display_flush_ready(display);
}

static void ui_update_menu_highlight(void)
{
    const uint8_t selected = ui_menu_selected_index(&s_menu);
    for (uint8_t index = 0; index < UI_MENU_ITEM_COUNT; ++index) {
        const bool is_selected = index == selected;
        // Raised tile plus accent text, the way the player screen marks what
        // it is playing. The arrow the old highlight needed is gone: a filled
        // row says the same thing without spending a character on it.
        lv_obj_set_style_bg_color(s_menu_rows[index],
                                  lv_color_hex(is_selected ? UI_COLOR_SELECTED
                                                           : UI_COLOR_GROUND), 0);
        lv_obj_set_style_bg_opa(s_menu_rows[index], LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(s_menu_rows[index],
                                    lv_color_hex(is_selected ? UI_COLOR_ACCENT
                                                             : UI_COLOR_MUTED), 0);
        lv_label_set_text(s_menu_rows[index], ui_menu_item_label((ui_menu_item_t)index));
        // The icon follows the text rather than staying lit: a row that is not
        // under the cursor should read as one thing, not as a bright mark with
        // dim writing next to it.
        lv_obj_set_style_image_recolor(s_menu_icons[index],
                                       lv_color_hex(is_selected ? UI_COLOR_ACCENT
                                                                : UI_COLOR_DIM), 0);
    }
}

static void ui_create_menu_screen(void)
{
    s_menu_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_menu_screen, lv_color_hex(UI_COLOR_GROUND), 0);
    lv_obj_set_style_border_width(s_menu_screen, 0, 0);
    lv_obj_set_style_pad_all(s_menu_screen, 0, 0);
    // Set once here rather than on each label: text_font is inherited in LVGL,
    // so this covers every label on the screen including any added later. The
    // default font has no Cyrillic and renders it as empty boxes, which is a
    // mistake that only shows up when a label first receives Russian text.
    lv_obj_set_style_text_font(s_menu_screen, &ui_font_cyrillic_20, 0);

    ui_status_strip_create(s_menu_screen, &s_menu_strip, "jRadio");

    for (uint8_t index = 0; index < UI_MENU_ITEM_COUNT; ++index) {
        s_menu_rows[index] = lv_label_create(s_menu_screen);
        lv_label_set_text(s_menu_rows[index], ui_menu_item_label((ui_menu_item_t)index));
        lv_obj_set_pos(s_menu_rows[index], UI_MENU_ROW_X,
                       UI_MENU_ROW_Y + index * UI_MENU_ROW_PITCH);
        lv_obj_set_size(s_menu_rows[index], UI_MENU_ROW_W, UI_MENU_ROW_H);
        // Left padding, not a second object: the text starts past the icon
        // column so a long name is shortened against the right edge only.
        lv_obj_set_style_pad_left(s_menu_rows[index], UI_MENU_TEXT_PAD, 0);
        lv_obj_set_style_pad_top(s_menu_rows[index], 1, 0);
        lv_obj_set_style_radius(s_menu_rows[index], 3, 0);
        lv_label_set_long_mode(s_menu_rows[index], LV_LABEL_LONG_DOT);
    }
    /* Created after every row so they sit above the highlighted background:
     * LVGL draws children in creation order and the row tile is opaque. */
    for (uint8_t index = 0; index < UI_MENU_ITEM_COUNT; ++index) {
        s_menu_icons[index] = lv_image_create(s_menu_screen);
        lv_obj_remove_style_all(s_menu_icons[index]);
        lv_image_set_src(s_menu_icons[index],
                         ui_feed_icon_image((ui_feed_item_t)index, UI_FEED_ICON_SMALL));
        lv_obj_set_size(s_menu_icons[index], UI_FEED_ICON_SMALL_PX, UI_FEED_ICON_SMALL_PX);
        lv_obj_set_pos(s_menu_icons[index], UI_MENU_ICON_X,
                       UI_MENU_ROW_Y + index * UI_MENU_ROW_PITCH +
                           (UI_MENU_ROW_H - UI_FEED_ICON_SMALL_PX) / 2);
        lv_obj_set_style_image_recolor_opa(s_menu_icons[index], LV_OPA_COVER, 0);
    }

    // Not a key hint: the only thing this line ever says is why a source
    // refused to open, and it is empty the rest of the time.
    s_menu_notice = lv_label_create(s_menu_screen);
    lv_label_set_text(s_menu_notice, "");
    lv_obj_set_style_text_font(s_menu_notice, &ui_font_cyrillic_14, 0);
    lv_obj_set_pos(s_menu_notice, 12, 221);
    lv_obj_set_style_text_color(s_menu_notice, lv_color_hex(UI_COLOR_NOTICE), 0);
    ui_update_menu_highlight();
}

// Fills `text` with what the row at `list_index` should read, and reports
// whether that row is the one currently playing and whether it is a directory.
/* Longest row either list can produce: a full-length USB name. Station rows
 * are far shorter - a three-digit ordinal, a space and a 96-byte name. */
#define UI_LIST_ROW_TEXT_MAX (FILE_BROWSER_NAME_MAX_LEN + 8U)

static bool ui_list_row_text(size_t list_index, char *text, size_t text_size,
                             bool *active, bool *directory)
{
    *active = false;
    *directory = false;
    if (!ui_list_shows_files()) {
        const station_catalog_entry_t *entry = player_control_station_at(list_index);
        // Ordinal, so the first station reads 001 rather than 000.
        snprintf(text, text_size, "%03u %s", (unsigned)(list_index + 1U),
                 entry == NULL ? "" : entry->name);
        *active = list_index == station_list_active_index(&s_station_list);
        return true;
    }
    if (s_file_browser_has_parent_row && list_index == 0U) {
        snprintf(text, text_size, "..");
        return true;
    }
    file_browser_entry_t entry;
    if (!player_control_file_entry_at(list_index - ui_files_row_offset(), &entry)) {
        text[0] = '\0';
        return false;
    }
    // Directories are marked rather than merely sorted first, so the row tells
    // you what the encoder click will do before you press it. The mark itself
    // is drawn by the caller, from its own label - see s_station_list_icons.
    *directory = entry.kind == FILE_BROWSER_ENTRY_DIRECTORY;
    snprintf(text, text_size, "%s", entry.name);
    *active = list_index == station_list_active_index(&s_station_list);
    return true;
}

/* Last element of a path, for the strip's left slot.
 *
 * The slot is 116 px, so "/usb0/Music/Jazz" would arrive ellipsised from the
 * right - cutting off exactly the part that says where you are. The folder's
 * own name is both shorter and the more useful half. */
static const char *ui_path_leaf(const char *path)
{
    const char *fallback = ui_source_short_name(ui_player_state_source(&s_player_ui));
    if (path == NULL || path[0] == '\0') return fallback;
    const char *slash = strrchr(path, '/');
    const char *leaf = slash != NULL ? slash + 1 : path;
    // The mount point itself is not a folder anyone named; say what it is.
    return file_browser_path_is_root(path) || leaf[0] == '\0' ? fallback : leaf;
}

static void ui_update_station_list(void)
{
    size_t cursor_row = 0U;
    const int count = (int)s_station_list.count;
    const int window_top = station_list_window_top(&s_station_list,
                                                   UI_STATION_LIST_MAX_ROWS, &cursor_row);
    for (size_t row = 0; row < UI_STATION_LIST_MAX_ROWS; ++row) {
        const int entry_index = window_top + (int)row;
        if (entry_index < 0 || entry_index >= count) {
            // Padding: the cursor stays on the middle row, so the rows beyond
            // either end of the catalogue are simply blank.
            lv_obj_add_flag(s_station_list_rows[row], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_station_list_icons[row], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(s_station_list_rows[row], LV_OBJ_FLAG_HIDDEN);
        char text[UI_LIST_ROW_TEXT_MAX];
        bool active = false;
        bool directory = false;
        (void)ui_list_row_text((size_t)entry_index, text, sizeof(text), &active,
                               &directory);
        // The name starts after the folder mark on a directory row and at the
        // edge everywhere else, so the two never overlap and a file's name is
        // not indented for a mark it does not have.
        if (directory) {
            lv_obj_clear_flag(s_station_list_icons[row], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_station_list_icons[row], LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_set_style_pad_left(s_station_list_rows[row],
                                  directory ? 8 + UI_LIST_ICON_W : 8, 0);
        const bool selected = row == cursor_row;
        lv_obj_set_style_bg_color(s_station_list_rows[row],
                                  lv_color_hex(selected ? UI_COLOR_SELECTED
                                                        : UI_COLOR_GROUND), 0);
        lv_obj_set_style_bg_opa(s_station_list_rows[row], LV_OPA_COVER, 0);
        /* Three states, and each one is a brightness rather than a shape. The
         * cursor takes the accent, the row that is playing is the brightest of
         * the rest, everything else stays muted.
         *
         * The playing row used to be ringed with an outline instead. Two marks
         * of different kinds competed for the same glance - a rectangle and a
         * lit row - and the rectangle was the one that had to be decoded. */
        const uint32_t row_colour = selected ? UI_COLOR_ACCENT
                                    : active ? UI_COLOR_TEXT
                                             : UI_COLOR_MUTED;
        lv_obj_set_style_text_color(s_station_list_rows[row], lv_color_hex(row_colour), 0);
        // Only the row under the cursor scrolls: a screen of six marquees at
        // once is unreadable, and the row being pointed at is the one whose
        // full name the user is actually after. The others keep the ellipsis.
        //
        // Set before the text, since changing either restarts the animation -
        // which is why this function is only called when something really
        // changed, never from the poll loop.
        lv_label_set_long_mode(s_station_list_rows[row],
                               selected ? LV_LABEL_LONG_MODE_SCROLL
                                        : LV_LABEL_LONG_MODE_DOTS);
        lv_label_set_text(s_station_list_rows[row], text);
    }
    // Hidden when there is nothing to scroll through at all - on the "no
    // drive" screen a full bar under an empty list would be nonsense.
    if (s_station_list.count == 0U) {
        lv_obj_add_flag(s_station_list_progress, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(s_station_list_progress, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(s_station_list_progress,
                         station_list_progress_percent(&s_station_list), LV_ANIM_OFF);
    }
}

static void ui_load_menu_screen(void);

static void ui_create_settings_screen(void)
{
    s_settings_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_settings_screen, lv_color_hex(UI_COLOR_GROUND), 0);
    lv_obj_set_style_border_width(s_settings_screen, 0, 0);
    lv_obj_set_style_pad_all(s_settings_screen, 0, 0);
    // Set once here rather than on each label: text_font is inherited in LVGL,
    // so this covers every label on the screen including any added later. The
    // default font has no Cyrillic and renders it as empty boxes, which is a
    // mistake that only shows up when a label first receives Russian text.
    lv_obj_set_style_text_font(s_settings_screen, &ui_font_cyrillic_14, 0);

    lv_obj_t *title = lv_label_create(s_settings_screen);
    lv_label_set_text(title, "Настройки");
    lv_obj_set_pos(title, 12, 8);
    // Matches the group headings under it: a heading smaller than the rows it
    // introduces reads as a mistake.
    lv_obj_set_style_text_font(title, &ui_font_cyrillic_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_TEXT), 0);

    for (size_t row = 0; row < UI_SETTINGS_MAX_ROWS; ++row) {
        s_settings_rows[row] = lv_label_create(s_settings_screen);
        lv_obj_set_pos(s_settings_rows[row], 10, UI_SET_ROW_Y + (int)row * UI_SET_ROW_PITCH);
        lv_obj_set_width(s_settings_rows[row], 300);
        lv_obj_set_height(s_settings_rows[row], UI_SET_ROW_H);
        lv_obj_set_style_pad_left(s_settings_rows[row], 6, 0);
        lv_obj_set_style_pad_top(s_settings_rows[row], 2, 0);
        lv_label_set_long_mode(s_settings_rows[row], LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(s_settings_rows[row], lv_color_hex(UI_COLOR_TEXT), 0);
        lv_obj_set_style_bg_opa(s_settings_rows[row], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(s_settings_rows[row], lv_color_hex(UI_COLOR_GROUND), 0);
        lv_label_set_text(s_settings_rows[row], "");
    }
    for (size_t index = 0; index < UI_SETTINGS_SWITCH_COUNT; ++index) {
        s_settings_switches[index] = lv_switch_create(s_settings_screen);
        lv_obj_set_size(s_settings_switches[index], 42, 20);
        lv_obj_set_style_bg_color(s_settings_switches[index], lv_color_hex(0x546E7A), LV_PART_MAIN);
        // On is the accent, the same colour the selected icon and the playing
        // row use. The teal it replaced was the only colour on the device that
        // meant nothing anywhere else.
        lv_obj_set_style_bg_color(s_settings_switches[index], lv_color_hex(UI_COLOR_ACCENT),
                                  LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(s_settings_switches[index], lv_color_hex(0xECEFF1), LV_PART_KNOB);
        lv_obj_set_style_bg_color(s_settings_switches[index], lv_color_hex(UI_COLOR_TEXT),
                                  LV_PART_KNOB | LV_STATE_CHECKED);
        lv_obj_add_flag(s_settings_switches[index], LV_OBJ_FLAG_HIDDEN);
    }
    s_settings_notice = lv_label_create(s_settings_screen);
    lv_obj_set_pos(s_settings_notice, 10, UI_SET_BAND_Y - 24);
    lv_obj_set_width(s_settings_notice, 300);
    lv_label_set_long_mode(s_settings_notice, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_settings_notice, lv_color_hex(0xFFCC80), 0);
    lv_obj_set_style_bg_opa(s_settings_notice, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_settings_notice, lv_color_hex(UI_COLOR_GROUND), 0);
    lv_label_set_text(s_settings_notice, "");

    /* Its own band rather than another row: the address is not a setting, and
     * a row would scroll with the groups and could be covered by the notice. */
    s_settings_web_band = lv_obj_create(s_settings_screen);
    lv_obj_remove_style_all(s_settings_web_band);
    lv_obj_set_pos(s_settings_web_band, 0, UI_SET_BAND_Y);
    lv_obj_set_size(s_settings_web_band, 320, UI_SET_BAND_H);
    lv_obj_set_style_bg_color(s_settings_web_band, lv_color_hex(UI_COLOR_STRIP), 0);
    lv_obj_set_style_bg_opa(s_settings_web_band, LV_OPA_COVER, 0);

    lv_obj_t *web_tag = lv_label_create(s_settings_web_band);
    lv_label_set_text(web_tag, "web");
    lv_obj_set_pos(web_tag, 12, (UI_SET_BAND_H - 19) / 2);
    lv_obj_set_style_text_color(web_tag, lv_color_hex(UI_COLOR_ACCENT), 0);

    s_settings_web_address = lv_label_create(s_settings_web_band);
    lv_obj_set_pos(s_settings_web_address, 52, (UI_SET_BAND_H - 19) / 2);
    lv_obj_set_width(s_settings_web_address, 256);
    lv_label_set_long_mode(s_settings_web_address, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_settings_web_address, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_label_set_text(s_settings_web_address, "");
}

static const char *ui_settings_group_text(ui_settings_group_t group, bool english)
{
    switch (group) {
    case UI_SETTINGS_GROUP_LANGUAGE: return english ? "Language" : "Язык";
    case UI_SETTINGS_GROUP_GENERAL: return english ? "General" : "Общие";
    case UI_SETTINGS_GROUP_DISPLAY: return english ? "Display" : "Экран";
    default: return "";
    }
}

static void ui_settings_row_text(const ui_settings_row_t *row, char *text, size_t text_size)
{
    const bool english = s_device_settings.language == DEVICE_LANGUAGE_EN;
    const char *group = ui_settings_group_text(row->group, english);
    if (row->kind == UI_SETTINGS_ROW_GROUP) {
        snprintf(text, text_size, "%s", group);
        return;
    }
    switch (row->id) {
    case UI_SETTINGS_ROW_LANGUAGE_FIELD:
        snprintf(text, text_size, "  %s: %s", english ? "Language" : "Язык",
                 english ? "English" : "Русский");
        break;
    case UI_SETTINGS_ROW_HOME_SCREEN_FIELD:
        snprintf(text, text_size, "  %s: %s", english ? "Home screen" : "Главный экран",
                 s_device_settings.home_screen == DEVICE_HOME_SCREEN_FEED ?
                     (english ? "Feed" : "Лента") : (english ? "List" : "Список"));
        break;
    case UI_SETTINGS_ROW_AUTOPLAY_FIELD:
        snprintf(text, text_size, "  %s: %s", english ? "Autoplay" : "Автовоспроизведение",
                 s_device_settings.autoplay ? "ON" : "OFF");
        break;
    case UI_SETTINGS_ROW_FLIP_VERTICAL_FIELD:
        snprintf(text, text_size, "  %s: %s", english ? "Flip vertical" : "Поворот по вертикали",
                 s_device_settings.flip_vertical ? "ON" : "OFF");
        break;
    case UI_SETTINGS_ROW_FLIP_HORIZONTAL_FIELD:
        snprintf(text, text_size, "  %s: %s", english ? "Flip horizontal" : "Поворот по горизонтали",
                 s_device_settings.flip_horizontal ? "ON" : "OFF");
        break;
    default:
        text[0] = '\0';
        break;
    }
}

/* Maps a row to its switch, or answers false for rows that have none. Kept as
 * one lookup rather than a chain of ternaries: every boolean setting needs the
 * index, the current value and the "has a switch" test to agree, and three
 * separate expressions is how they stop agreeing. */
static bool ui_settings_row_switch(ui_settings_row_id_t id, size_t *index, bool *value)
{
    switch (id) {
    case UI_SETTINGS_ROW_AUTOPLAY_FIELD:
        *index = 0U;
        *value = s_device_settings.autoplay;
        return true;
    case UI_SETTINGS_ROW_FLIP_VERTICAL_FIELD:
        *index = 1U;
        *value = s_device_settings.flip_vertical;
        return true;
    case UI_SETTINGS_ROW_FLIP_HORIZONTAL_FIELD:
        *index = 2U;
        *value = s_device_settings.flip_horizontal;
        return true;
    default:
        return false;
    }
}

static void ui_update_settings_web_band(void)
{
    const wifi_provisioning_status_t status = wifi_provisioning_status();
    char text[64];
    ui_web_address_text(status.mode, status.ipv4,
                        s_device_settings.language == DEVICE_LANGUAGE_EN,
                        text, sizeof(text));
    ui_set_label_text_if_changed(s_settings_web_address, text);
}

static void ui_update_settings(void)
{
    if (!s_settings_open) return;
    ui_update_settings_web_band();
    const size_t row_count = ui_settings_model_row_count(&s_settings_model);
    const ui_settings_row_id_t selected = ui_settings_model_selected(&s_settings_model);
    for (size_t index = 0; index < UI_SETTINGS_SWITCH_COUNT; ++index) {
        lv_obj_add_flag(s_settings_switches[index], LV_OBJ_FLAG_HIDDEN);
    }
    for (size_t row = 0; row < UI_SETTINGS_MAX_ROWS; ++row) {
        if (row >= row_count) {
            /* Hidden, not merely blanked: a row's background is opaque, and
             * with the taller pitch an unused row reaches into the web band. */
            lv_label_set_text(s_settings_rows[row], "");
            lv_obj_add_flag(s_settings_rows[row], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(s_settings_rows[row], LV_OBJ_FLAG_HIDDEN);
        const ui_settings_row_t item = ui_settings_model_row_at(&s_settings_model, row);
        char text[96];
        ui_settings_row_text(&item, text, sizeof(text));
        ui_set_label_text_if_changed(s_settings_rows[row], text);
        const bool is_group = item.kind == UI_SETTINGS_ROW_GROUP;
        // Set per row rather than at creation: which row is a heading changes
        // as groups open and close.
        lv_obj_set_style_text_font(s_settings_rows[row],
                                   is_group ? &ui_font_cyrillic_20 : &ui_font_cyrillic_14, 0);
        lv_obj_set_style_pad_top(s_settings_rows[row], is_group ? 1 : 4, 0);
        const bool selected_row = item.id == selected;
        const uint32_t background = selected_row ? UI_COLOR_SELECTED :
            item.kind == UI_SETTINGS_ROW_FIELD ? 0x263746 : 0x101820;
        size_t switch_index = 0U;
        bool enabled = false;
        const bool has_switch = ui_settings_row_switch(item.id, &switch_index, &enabled);
        lv_obj_set_x(s_settings_rows[row], has_switch ? 58 : 10);
        lv_obj_set_width(s_settings_rows[row], has_switch ? 242 : 300);
        lv_obj_set_style_bg_color(s_settings_rows[row], lv_color_hex(background), 0);
        if (has_switch) {
            lv_obj_t *toggle = s_settings_switches[switch_index];
            if (enabled) lv_obj_add_state(toggle, LV_STATE_CHECKED);
            else lv_obj_clear_state(toggle, LV_STATE_CHECKED);
            lv_obj_set_pos(toggle, 10,
                           UI_SET_ROW_Y + (int)row * UI_SET_ROW_PITCH + (UI_SET_ROW_H - 20) / 2);
            lv_obj_clear_flag(toggle, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void ui_show_settings(void)
{
    s_settings_open = true;
    ui_settings_model_init(&s_settings_model);
    if (!device_settings_init(&s_device_settings)) {
        lv_label_set_text(s_settings_notice, "Ошибка чтения settings.csv");
    } else {
        lv_label_set_text(s_settings_notice, "");
        (void)board_display_set_rotation(s_device_settings.flip_vertical,
                                         s_device_settings.flip_horizontal);
    }
    ui_update_settings();
    lv_screen_load(s_settings_screen);
}

static void ui_close_settings(void)
{
    if (!s_settings_open) return;
    s_settings_open = false;
    ui_load_menu_screen();
}

static void ui_settings_change_selected(void)
{
    const ui_settings_row_id_t selected = ui_settings_model_selected(&s_settings_model);
    bool changed = false;
    switch (selected) {
    case UI_SETTINGS_ROW_LANGUAGE_FIELD:
        changed = device_settings_set_language(
            &s_device_settings,
            s_device_settings.language == DEVICE_LANGUAGE_EN ? DEVICE_LANGUAGE_RU : DEVICE_LANGUAGE_EN);
        break;
    case UI_SETTINGS_ROW_HOME_SCREEN_FIELD:
        changed = device_settings_set_home_screen(
            &s_device_settings,
            s_device_settings.home_screen == DEVICE_HOME_SCREEN_FEED ?
                DEVICE_HOME_SCREEN_TEXT : DEVICE_HOME_SCREEN_FEED);
        break;
    case UI_SETTINGS_ROW_AUTOPLAY_FIELD:
        changed = device_settings_set_autoplay(&s_device_settings,
                                               !s_device_settings.autoplay);
        break;
    case UI_SETTINGS_ROW_FLIP_VERTICAL_FIELD:
        changed = device_settings_set_flip_vertical(&s_device_settings,
                                                    !s_device_settings.flip_vertical);
        if (changed) {
            (void)board_display_set_rotation(s_device_settings.flip_vertical,
                                             s_device_settings.flip_horizontal);
        }
        break;
    case UI_SETTINGS_ROW_FLIP_HORIZONTAL_FIELD:
        changed = device_settings_set_flip_horizontal(&s_device_settings,
                                                      !s_device_settings.flip_horizontal);
        if (changed) {
            (void)board_display_set_rotation(s_device_settings.flip_vertical,
                                             s_device_settings.flip_horizontal);
        }
        break;
    default:
        return;
    }
    lv_label_set_text(s_settings_notice, changed ? "" : "Ошибка записи settings.csv");
}

static void ui_create_station_list_screen(void)
{
    s_station_list_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_station_list_screen, lv_color_hex(UI_COLOR_GROUND), 0);
    lv_obj_set_style_border_width(s_station_list_screen, 0, 0);
    lv_obj_set_style_pad_all(s_station_list_screen, 0, 0);
    // Set once here rather than on each label: text_font is inherited in LVGL,
    // so this covers every label on the screen including any added later. The
    // default font has no Cyrillic and renders it as empty boxes, which is a
    // mistake that only shows up when a label first receives Russian text.
    lv_obj_set_style_text_font(s_station_list_screen, &ui_font_cyrillic_14, 0);
    ui_status_strip_create(s_station_list_screen, &s_list_strip, "");
    /* The heading moves into the strip's left slot instead of taking a line of
     * its own - which is where the room for the rule and the position bar came
     * from, without giving up a row of the list. */
    s_station_list_title = s_list_strip.context;

    s_station_list_rule = lv_obj_create(s_station_list_screen);
    lv_obj_set_pos(s_station_list_rule, 10, UI_LIST_RULE_Y);
    lv_obj_set_size(s_station_list_rule, 300, 1);
    lv_obj_set_style_bg_color(s_station_list_rule, lv_color_hex(UI_COLOR_RULE), 0);
    lv_obj_set_style_border_width(s_station_list_rule, 0, 0);
    lv_obj_set_style_pad_all(s_station_list_rule, 0, 0);
    lv_obj_clear_flag(s_station_list_rule, LV_OBJ_FLAG_SCROLLABLE);

    // Same shape and colours as the track bar on the player screen: a thin
    // full-width line whose filled part is the accent.
    s_station_list_progress = lv_bar_create(s_station_list_screen);
    lv_obj_set_pos(s_station_list_progress, 10, UI_LIST_PROGRESS_Y);
    lv_obj_set_size(s_station_list_progress, 300, 4);
    lv_bar_set_range(s_station_list_progress, 0, 100);
    lv_obj_set_style_radius(s_station_list_progress, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(s_station_list_progress, 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_station_list_progress, lv_color_hex(0x23303C), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_station_list_progress, lv_color_hex(UI_COLOR_ACCENT),
                              LV_PART_INDICATOR);
    for (size_t row = 0; row < UI_STATION_LIST_MAX_ROWS; ++row) {
        s_station_list_rows[row] = lv_label_create(s_station_list_screen);
        lv_obj_set_pos(s_station_list_rows[row], 10,
                       UI_LIST_ROW_Y + (int)row * UI_LIST_ROW_PITCH);
        lv_obj_set_size(s_station_list_rows[row], 300, UI_LIST_ROW_H);
        // Bigger than the rest of the screen on purpose: this is the text the
        // user reads from a distance while turning the encoder. The 23 px line
        // it needs is what set the row height and the pitch above.
        lv_obj_set_style_text_font(s_station_list_rows[row], &ui_font_cyrillic_20, 0);
        lv_obj_set_style_pad_left(s_station_list_rows[row], 8, 0);
        lv_obj_set_style_pad_top(s_station_list_rows[row], 3, 0);
        lv_obj_set_style_radius(s_station_list_rows[row], 3, 0);
        lv_label_set_long_mode(s_station_list_rows[row], LV_LABEL_LONG_MODE_DOTS);
    }
    /* Created after every row, so each mark is drawn over the row background
     * rather than under it - LVGL paints children in creation order, and the
     * rows are opaque. */
    for (size_t row = 0; row < UI_STATION_LIST_MAX_ROWS; ++row) {
        s_station_list_icons[row] = lv_label_create(s_station_list_screen);
        // 2 px down from the row: the glyph sits 4 px below the top of its own
        // line box and is 18 px tall, which centres it in the 30 px row.
        // x lines the mark up with where a file's name starts, so the two
        // kinds of row share a left edge instead of stepping in and out.
        lv_obj_set_pos(s_station_list_icons[row], 18,
                       UI_LIST_ROW_Y + (int)row * UI_LIST_ROW_PITCH + 2);
        lv_obj_set_style_text_font(s_station_list_icons[row], &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(s_station_list_icons[row],
                                    lv_color_hex(UI_COLOR_FOLDER), 0);
        lv_label_set_text(s_station_list_icons[row], LV_SYMBOL_DIRECTORY);
        lv_obj_add_flag(s_station_list_icons[row], LV_OBJ_FLAG_HIDDEN);
    }

    /* Created last, after every row, for the same reason the folder marks are:
     * the rows are opaque, and LVGL paints children in creation order. The
     * notice used to be created first and was drawn underneath them - which is
     * how "insert a drive" ended up invisible behind a screen of blank rows. */
    s_station_list_notice_icon = lv_image_create(s_station_list_screen);
    lv_obj_remove_style_all(s_station_list_notice_icon);
    lv_image_set_src(s_station_list_notice_icon, &ui_feed_icon_usb_stick_48);
    lv_obj_set_size(s_station_list_notice_icon, UI_LIST_NOTICE_ICON, UI_LIST_NOTICE_ICON);
    lv_image_set_inner_align(s_station_list_notice_icon, LV_IMAGE_ALIGN_CENTER);
    lv_obj_set_pos(s_station_list_notice_icon, (320 - UI_LIST_NOTICE_ICON) / 2,
                   UI_LIST_NOTICE_ICON_Y);
    lv_obj_set_style_image_recolor(s_station_list_notice_icon,
                                   lv_color_hex(UI_COLOR_NOTICE), 0);
    lv_obj_set_style_image_recolor_opa(s_station_list_notice_icon, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_station_list_notice_icon, LV_OBJ_FLAG_HIDDEN);

    s_station_list_notice = lv_label_create(s_station_list_screen);
    lv_label_set_text(s_station_list_notice, "");
    lv_obj_set_pos(s_station_list_notice, 10, UI_LIST_NOTICE_TEXT_Y);
    lv_obj_set_width(s_station_list_notice, 300);
    // Centred under the drive, and wrapped rather than ellipsised: the
    // unreadable-drive line is two lines wide and its second half - the format
    // to use - is the half worth reading.
    lv_obj_set_style_text_align(s_station_list_notice, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_station_list_notice, &ui_font_cyrillic_20, 0);
    lv_obj_set_style_text_color(s_station_list_notice, lv_color_hex(UI_COLOR_NOTICE), 0);
}

static void ui_show_station_list(void);
// Defined with the rest of the scrubbing mode, below the command helpers it
// needs; every way off the player screen has to close the mode first.
static void ui_end_seek(void);

/* Player screen layout, in device pixels.
 *
 * 320x240 leaves no room for guessing, so the numbers live together here
 * rather than scattered through the builder. A status strip carries what is
 * not about the music - clock, signal - and everything below it is the
 * playing item. The bottom row pairs the two things that answer "is it going
 * to keep playing, and how loud": buffer on the left, volume on the right.
 */
#define UI_SRC_STATUS_H 26
#define UI_SRC_ART_X 10
#define UI_SRC_ART_Y 36
#define UI_SRC_ART_SIZE 96
#define UI_SRC_TEXT_X 118
#define UI_SRC_TEXT_W 192
/* One line of each face, from the generated fonts' own line_height. Rows are
 * spaced by these rather than by eye, so nothing can overlap its neighbour. */
#define UI_SRC_LINE_H 19
#define UI_SRC_TRACK_H 23
#define UI_SRC_ROW_TRACK 58
#define UI_SRC_ROW_ARTIST 86
#define UI_SRC_ROW_STREAM 108
#define UI_SRC_RULE_TOP 144
#define UI_SRC_VU_Y 155
#define UI_SRC_VU_BLOCK_H 10
#define UI_SRC_VU_PITCH 18
/* The two rules frame the meter, so the gap below is derived from the gap
 * above instead of being typed in again. Written out by hand they drifted to
 * 10 and 17 px, which read as the meter having slipped upwards. */
#define UI_SRC_VU_GAP (UI_SRC_VU_Y - (UI_SRC_RULE_TOP + 1))
#define UI_SRC_VU_BOTTOM (UI_SRC_VU_Y + UI_SRC_VU_PITCH + UI_SRC_VU_BLOCK_H)
#define UI_SRC_RULE_BOTTOM (UI_SRC_VU_BOTTOM + UI_SRC_VU_GAP)
/* Equalising the frame freed seven pixels; they go into the space around the
 * progress bar, which was pressed against the rule above it and the times
 * below. */
#define UI_SRC_PROGRESS_Y (UI_SRC_RULE_BOTTOM + 8)
/* A hairline while it only reports, twice that while it is being aimed: the
 * bar is the control in scrubbing mode, and a 4 px target is not one. It grows
 * about its own centre line, so the row it lives on does not shift. */
#define UI_SRC_PROGRESS_H 4
#define UI_SRC_PROGRESS_SEEK_H 8
#define UI_SRC_FOOT_Y 214
#define UI_SRC_PAUSE_SIZE 76
#define UI_SRC_PAUSE_BAR_W 10
#define UI_SRC_PAUSE_BAR_H 34
#define UI_SRC_PAUSE_GAP 10

/* Picks up whatever cover album_art has published.
 *
 * Runs on the poll loop but costs one comparison until the generation moves,
 * which happens once per track at most. The pixels are copied rather than
 * pointed at: LVGL redraws the image whenever the area is invalidated, long
 * after this returns, and the playback task owns the other copy. */
static void ui_update_cover(void)
{
    if (s_source_cover == NULL || s_source_cover_pixels == NULL) return;
    const album_art_status_t status = album_art_status();
    if (status.generation == s_source_cover_generation) return;

    uint16_t width = 0U;
    uint16_t height = 0U;
    const bool shown =
        status.present && album_art_copy(s_source_cover_pixels, ALBUM_ART_PIXELS, &width, &height);
    s_source_cover_generation = status.generation;
    if (!shown) {
        // Back to the placeholder, which is what the tile shows for a file
        // with no cover and for the radio.
        lv_obj_add_flag(s_source_cover, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    s_source_cover_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_source_cover_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    s_source_cover_dsc.header.w = width;
    s_source_cover_dsc.header.h = height;
    s_source_cover_dsc.header.stride = (uint32_t)width * sizeof(uint16_t);
    s_source_cover_dsc.data_size = (uint32_t)width * height * sizeof(uint16_t);
    s_source_cover_dsc.data = (const uint8_t *)s_source_cover_pixels;
    // Re-set even when the pointer has not changed: this is what makes LVGL
    // re-read the header, and the size in it changes with the picture.
    lv_image_set_src(s_source_cover, &s_source_cover_dsc);
    /* Centred on the tile. A picture that is not square is fitted inside the
     * square rather than stretched to it, so the margins are uneven. */
    lv_obj_set_pos(s_source_cover, UI_SRC_ART_X + (int)(UI_SRC_ART_SIZE - width) / 2,
                   UI_SRC_ART_Y + (int)(UI_SRC_ART_SIZE - height) / 2);
    lv_obj_clear_flag(s_source_cover, LV_OBJ_FLAG_HIDDEN);
}

static void ui_create_source_screen(void)
{
    s_source_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_source_screen, lv_color_hex(UI_COLOR_GROUND), 0);
    lv_obj_set_style_border_width(s_source_screen, 0, 0);
    lv_obj_set_style_pad_all(s_source_screen, 0, 0);
    // Set once here rather than on each label: text_font is inherited in LVGL,
    // so this covers every label on the screen including any added later. The
    // default font has no Cyrillic and renders it as empty boxes, which is a
    // mistake that only shows up when a label first receives Russian text.
    lv_obj_set_style_text_font(s_source_screen, &ui_font_cyrillic_14, 0);

    ui_status_strip_create(s_source_screen, &s_source_strip, "");

    // Stands in for the cover art that is not implemented yet. A symbol on a
    // tile keeps the composition; an empty square would read as a fault.
    s_source_art = lv_label_create(s_source_screen);
    lv_obj_set_pos(s_source_art, UI_SRC_ART_X, UI_SRC_ART_Y);
    lv_obj_set_size(s_source_art, UI_SRC_ART_SIZE, UI_SRC_ART_SIZE);
    lv_obj_set_style_bg_color(s_source_art, lv_color_hex(UI_COLOR_TILE), 0);
    lv_obj_set_style_bg_opa(s_source_art, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_source_art, lv_color_hex(UI_COLOR_TILE_EDGE), 0);
    lv_obj_set_style_border_width(s_source_art, 1, 0);
    lv_obj_set_style_radius(s_source_art, 3, 0);
    lv_obj_set_style_text_color(s_source_art, lv_color_hex(0x3E5060), 0);
    lv_obj_set_style_text_font(s_source_art, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_align(s_source_art, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(s_source_art, 24, 0);
    lv_label_set_text(s_source_art, LV_SYMBOL_AUDIO);

    // Created here and left empty: it is given a source the first time a file
    // with a cover is opened, and hidden again whenever there is none.
    s_source_cover = lv_image_create(s_source_screen);
    lv_obj_add_flag(s_source_cover, LV_OBJ_FLAG_HIDDEN);

    // Every one of these gets an explicit height of exactly one line. Without
    // it LV_LABEL_LONG_DOT wraps to a second line before it considers
    // shortening, and a long station name grew downwards over the codec row.
    s_source_title = lv_label_create(s_source_screen);
    lv_obj_set_pos(s_source_title, UI_SRC_TEXT_X, UI_SRC_ART_Y);
    lv_obj_set_size(s_source_title, UI_SRC_TEXT_W, UI_SRC_LINE_H);
    lv_label_set_long_mode(s_source_title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_source_title, lv_color_hex(UI_COLOR_ACCENT), 0);

    s_source_detail = lv_label_create(s_source_screen);
    lv_obj_set_pos(s_source_detail, UI_SRC_TEXT_X, UI_SRC_ROW_TRACK);
    lv_obj_set_size(s_source_detail, UI_SRC_TEXT_W, UI_SRC_TRACK_H);
    lv_obj_set_style_text_font(s_source_detail, &ui_font_cyrillic_20, 0);
    lv_label_set_long_mode(s_source_detail, LV_LABEL_LONG_SCROLL);
    lv_obj_set_style_text_color(s_source_detail, lv_color_hex(UI_COLOR_TEXT), 0);

    s_source_artist = lv_label_create(s_source_screen);
    lv_obj_set_pos(s_source_artist, UI_SRC_TEXT_X, UI_SRC_ROW_ARTIST);
    lv_obj_set_size(s_source_artist, UI_SRC_TEXT_W, UI_SRC_LINE_H);
    lv_label_set_long_mode(s_source_artist, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_source_artist, lv_color_hex(UI_COLOR_MUTED), 0);

    // Shares the performer row rather than getting one of its own: the two are
    // never both set, and a separate row would have to come out of the codec
    // line - which is where it used to land, on top of it.
    s_source_status = lv_label_create(s_source_screen);
    lv_obj_set_pos(s_source_status, UI_SRC_TEXT_X, UI_SRC_ROW_ARTIST);
    lv_obj_set_size(s_source_status, UI_SRC_TEXT_W, UI_SRC_LINE_H);
    lv_label_set_long_mode(s_source_status, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_source_status, lv_color_hex(UI_COLOR_DIM), 0);

    s_source_stream = lv_label_create(s_source_screen);
    lv_obj_set_pos(s_source_stream, UI_SRC_TEXT_X, UI_SRC_ROW_STREAM);
    lv_obj_set_size(s_source_stream, UI_SRC_TEXT_W, UI_SRC_LINE_H);
    lv_label_set_long_mode(s_source_stream, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_source_stream, lv_color_hex(UI_COLOR_DIM), 0);

    /* Brighter than the meter's own unlit blocks, which looks backwards for a
     * divider until you remember it is one pixel tall: a hairline loses far
     * more perceived contrast than a solid block of the same colour, and at
     * 0x23303C these were there in principle and invisible in practice. */
    lv_obj_t *rule_top = lv_obj_create(s_source_screen);
    lv_obj_set_pos(rule_top, 10, UI_SRC_RULE_TOP);
    lv_obj_set_size(rule_top, 300, 1);
    lv_obj_set_style_bg_color(rule_top, lv_color_hex(UI_COLOR_RULE), 0);
    lv_obj_set_style_border_width(rule_top, 0, 0);
    lv_obj_set_style_pad_all(rule_top, 0, 0);
    lv_obj_clear_flag(rule_top, LV_OBJ_FLAG_SCROLLABLE);

    for (size_t channel = 0; channel < 2U; ++channel) {
        for (size_t segment = 0; segment < UI_VU_SEGMENTS; ++segment) {
            lv_obj_t *block = lv_obj_create(s_source_screen);
            lv_obj_set_size(block, UI_VU_SEGMENT_W, UI_SRC_VU_BLOCK_H);
            lv_obj_set_pos(block,
                           30 + (int)segment * (UI_VU_SEGMENT_W + UI_VU_SEGMENT_GAP),
                           UI_SRC_VU_Y + (int)channel * UI_SRC_VU_PITCH);
            lv_obj_set_style_border_width(block, 0, 0);
            lv_obj_set_style_radius(block, 1, 0);
            lv_obj_set_style_pad_all(block, 0, 0);
            // Scrolling is on by default for lv_obj and would let these tiny
            // blocks take the encoder's input away from the screen.
            lv_obj_clear_flag(block, LV_OBJ_FLAG_SCROLLABLE);
            s_source_vu[channel][segment] = block;
        }
        lv_obj_t *mark = lv_label_create(s_source_screen);
        lv_label_set_text(mark, channel == 0U ? "L" : "R");
        lv_obj_set_pos(mark, 12, UI_SRC_VU_Y - 3 + (int)channel * UI_SRC_VU_PITCH);
        lv_obj_set_style_text_color(mark, lv_color_hex(UI_COLOR_DIM), 0);
    }

    lv_obj_t *rule_bottom = lv_obj_create(s_source_screen);
    lv_obj_set_pos(rule_bottom, 10, UI_SRC_RULE_BOTTOM);
    lv_obj_set_size(rule_bottom, 300, 1);
    lv_obj_set_style_bg_color(rule_bottom, lv_color_hex(UI_COLOR_RULE), 0);
    lv_obj_set_style_border_width(rule_bottom, 0, 0);
    lv_obj_set_style_pad_all(rule_bottom, 0, 0);
    lv_obj_clear_flag(rule_bottom, LV_OBJ_FLAG_SCROLLABLE);

    /* Track position. Sits under the bottom rule rather than in the footer
     * row: it belongs to what is playing, not to the system readouts beside
     * it, and a full-width line reads as a distance travelled in a way a short
     * one squeezed between two labels does not. Hidden for radio, which has no
     * end to be a fraction of. */
    s_source_progress = lv_obj_create(s_source_screen);
    lv_obj_set_pos(s_source_progress, 10, UI_SRC_PROGRESS_Y);
    lv_obj_set_size(s_source_progress, 300, UI_SRC_PROGRESS_H);
    lv_obj_set_style_bg_color(s_source_progress, lv_color_hex(0x23303C), 0);
    lv_obj_set_style_border_width(s_source_progress, 0, 0);
    lv_obj_set_style_radius(s_source_progress, 2, 0);
    lv_obj_set_style_pad_all(s_source_progress, 0, 0);
    lv_obj_clear_flag(s_source_progress, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *played = lv_obj_create(s_source_progress);
    lv_obj_set_pos(played, 0, 0);
    lv_obj_set_size(played, 1, UI_SRC_PROGRESS_H);
    lv_obj_set_style_bg_color(played, lv_color_hex(UI_COLOR_ACCENT), 0);
    lv_obj_set_style_border_width(played, 0, 0);
    lv_obj_set_style_radius(played, 2, 0);
    lv_obj_set_style_pad_all(played, 0, 0);
    lv_obj_clear_flag(played, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_source_progress, LV_OBJ_FLAG_HIDDEN);

    s_source_buffer = lv_label_create(s_source_screen);
    lv_obj_set_pos(s_source_buffer, 10, UI_SRC_FOOT_Y);
    lv_obj_set_style_text_color(s_source_buffer, lv_color_hex(UI_COLOR_DIM), 0);

    /* Names the bar next to it. Without it the strip was just a rectangle in
     * the footer, indistinguishable from the progress bar above. Centred on
     * the bar's own middle line rather than on the footer row. */
    s_source_volume_icon = lv_image_create(s_source_screen);
    lv_obj_remove_style_all(s_source_volume_icon);
    lv_image_set_src(s_source_volume_icon, &ui_feed_icon_volume_16);
    lv_obj_set_size(s_source_volume_icon, 16, 16);
    lv_obj_set_pos(s_source_volume_icon, 178, UI_SRC_FOOT_Y + 1);
    lv_obj_set_style_image_recolor(s_source_volume_icon, lv_color_hex(UI_COLOR_MUTED), 0);
    lv_obj_set_style_image_recolor_opa(s_source_volume_icon, LV_OPA_COVER, 0);

    s_source_volume_bar = lv_obj_create(s_source_screen);
    lv_obj_set_pos(s_source_volume_bar, 200, UI_SRC_FOOT_Y + 5);
    lv_obj_set_size(s_source_volume_bar, 60, 8);
    lv_obj_set_style_bg_color(s_source_volume_bar, lv_color_hex(0x23303C), 0);
    lv_obj_set_style_border_width(s_source_volume_bar, 0, 0);
    lv_obj_set_style_radius(s_source_volume_bar, 2, 0);
    lv_obj_set_style_pad_all(s_source_volume_bar, 0, 0);
    lv_obj_clear_flag(s_source_volume_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *fill = lv_obj_create(s_source_volume_bar);
    lv_obj_set_pos(fill, 0, 0);
    lv_obj_set_size(fill, 60, 8);
    lv_obj_set_style_bg_color(fill, lv_color_hex(UI_COLOR_MUTED), 0);
    lv_obj_set_style_border_width(fill, 0, 0);
    lv_obj_set_style_radius(fill, 2, 0);
    lv_obj_set_style_pad_all(fill, 0, 0);
    lv_obj_clear_flag(fill, LV_OBJ_FLAG_SCROLLABLE);

    s_source_volume = lv_label_create(s_source_screen);
    lv_obj_set_pos(s_source_volume, 266, UI_SRC_FOOT_Y);
    lv_obj_set_style_text_color(s_source_volume, lv_color_hex(UI_COLOR_MUTED), 0);
    lv_label_set_text(s_source_volume, "");

    /* Pause badge, created last so it draws over everything else.
     *
     * A word in a corner competed with the performer for the same row and lost
     * either way. A symbol in the middle of the screen says the same thing
     * without needing a line to itself, and reads at a glance from across the
     * room - which the word never did. */
    s_source_pause = lv_obj_create(s_source_screen);
    lv_obj_set_size(s_source_pause, UI_SRC_PAUSE_SIZE, UI_SRC_PAUSE_SIZE);
    lv_obj_center(s_source_pause);
    lv_obj_set_style_radius(s_source_pause, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_source_pause, lv_color_hex(0x000000), 0);
    // Translucent rather than solid: the badge has to read as laid over the
    // screen, not as a hole punched in it.
    lv_obj_set_style_bg_opa(s_source_pause, LV_OPA_60, 0);
    lv_obj_set_style_border_color(s_source_pause, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_border_opa(s_source_pause, LV_OPA_30, 0);
    lv_obj_set_style_border_width(s_source_pause, 1, 0);
    lv_obj_set_style_pad_all(s_source_pause, 0, 0);
    lv_obj_clear_flag(s_source_pause, LV_OBJ_FLAG_SCROLLABLE);
    for (int bar = 0; bar < 2; ++bar) {
        lv_obj_t *stroke = lv_obj_create(s_source_pause);
        lv_obj_set_size(stroke, UI_SRC_PAUSE_BAR_W, UI_SRC_PAUSE_BAR_H);
        lv_obj_set_pos(stroke,
                       (UI_SRC_PAUSE_SIZE - (2 * UI_SRC_PAUSE_BAR_W + UI_SRC_PAUSE_GAP)) / 2 +
                           bar * (UI_SRC_PAUSE_BAR_W + UI_SRC_PAUSE_GAP),
                       (UI_SRC_PAUSE_SIZE - UI_SRC_PAUSE_BAR_H) / 2);
        // The muted tone the rest of the screen uses for secondary text, not
        // pure white: the badge should read clearly without being the
        // brightest thing on a dark panel.
        lv_obj_set_style_bg_color(stroke, lv_color_hex(UI_COLOR_MUTED), 0);
        lv_obj_set_style_bg_opa(stroke, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(stroke, 0, 0);
        lv_obj_set_style_radius(stroke, 2, 0);
        lv_obj_set_style_pad_all(stroke, 0, 0);
        lv_obj_clear_flag(stroke, LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_add_flag(s_source_pause, LV_OBJ_FLAG_HIDDEN);
}

static bool ui_submit_player_command(const player_command_t *command)
{
    if (!ui_player_state_can_post(&s_player_ui, command)) {
        ESP_LOGW(TAG, "player command rejected: busy or invalid; kind=%d",
                 command == NULL ? -1 : (int)command->kind);
        return false;
    }
    if (!player_control_post(command)) {
        ESP_LOGW(TAG, "player command queue full; kind=%d", (int)command->kind);
        return false;
    }
    if (!ui_player_state_apply_post_result(&s_player_ui, command, true,
                                           ui_tick_get_ms())) {
        ESP_LOGW(TAG, "player command rejected after post; kind=%d",
                 (int)command->kind);
        return false;
    }
    return true;
}

static void ui_load_source_screen(audio_source_t selected_source)
{
    (void)ui_menu_select_source(&s_menu, selected_source);
    /* And the same for the other home screen. Leaving the player should land
     * on the icon it was started from, whoever started it - a press on the
     * feed, autoplay at boot, or the web UI. Done here rather than at the
     * press that started it because those last two never touch this screen,
     * and this is the one place every route to the player passes through. */
    (void)ui_feed_model_select_source(&s_feed_model, selected_source);
    const uint8_t index = ui_menu_selected_index(&s_menu);
    lv_label_set_text(s_source_title, ui_menu_item_label((ui_menu_item_t)index));
    lv_screen_load(s_source_screen);
    if (selected_source == AUDIO_SOURCE_INTERNET_RADIO) {
        ui_set_state_line("Connecting...", "");
        ui_set_label_text_if_changed(s_source_detail, "");
        ui_set_label_text_if_changed(s_source_stream, "");
        s_waiting_for_radio_station = true;
        s_radio_station_wait_started_ms = ui_tick_get_ms();
    } else if (audio_source_is_files(selected_source)) {
        s_waiting_for_radio_station = false;
        // Nothing plays until a file is chosen, so this screen opens idle
        // rather than pretending to connect.
        ui_set_state_line("Выберите файл", "");
        ui_set_label_text_if_changed(s_source_detail, "");
        ui_set_label_text_if_changed(s_source_stream, "");
    } else {
        s_waiting_for_radio_station = false;
        ui_set_state_line("Not implemented", "");
        ui_set_label_text_if_changed(s_source_detail, "");
        ui_set_label_text_if_changed(s_source_stream, "");
    }
}

static void ui_show_source(void)
{
    const audio_source_t selected_source = ui_menu_activate(&s_menu);
    if (selected_source == AUDIO_SOURCE_NONE) return;
    // A drive that cannot be browsed still opens its screen - the list, empty,
    // saying why. Refusing to leave the menu left the user pressing a working
    // button with nothing happening but a line of small print.
    /* The drive is judged from what the snapshot last saw, because it is
     * mounted the whole time and that reading is current. The card is not:
     * nothing detects it being inserted, so its state is only ever as fresh as
     * the last attempt, and refusing on it would leave a card put in after
     * boot unreachable. Selecting the card therefore always tries - the mount
     * is the detection - and the notice arrives from the snapshot afterwards. */
    s_files_unavailable =
        selected_source == AUDIO_SOURCE_USB &&
        !ui_files_can_open(selected_source, s_last_usb_media, s_last_files_entry_count);
    s_files_unavailable_source = s_files_unavailable ? selected_source : AUDIO_SOURCE_NONE;
    ui_set_label_text_if_changed(s_menu_notice, "");
    const player_command_t command = {
        .kind = PLAYER_COMMAND_SELECT_SOURCE,
        .source = selected_source,
        .item_index = PLAYER_ITEM_NONE,
    };
    if (s_files_unavailable) {
        // No source to select, so drive the screen directly.
        s_player_ui.source = selected_source;
        ui_show_station_list();
        return;
    }
    if (audio_source_is_files(selected_source)) {
        s_files_list_open_revision = player_control_listing_revision();
        s_files_list_open_requested = true;
    }
    if (!ui_submit_player_command(&command)) return;
    // Both sources open on their list rather than the player: there is nothing
    // to look at on the player screen until something has been chosen.
    ui_show_station_list();
}

static void ui_load_menu_screen(void)
{
    s_waiting_for_radio_station = false;
    if (s_device_settings.home_screen == DEVICE_HOME_SCREEN_FEED && s_feed_screen != NULL) {
        ui_update_feed_screen();
        lv_screen_load(s_feed_screen);
        return;
    }
    ui_update_menu_highlight();
    lv_screen_load(s_menu_screen);
}

static void ui_show_menu(void)
{
    ui_end_seek();
    s_files_unavailable = false;
    s_files_unavailable_source = AUDIO_SOURCE_NONE;
    const ui_player_view_t old_view = ui_player_state_view(&s_player_ui);
    const audio_source_t old_source = ui_player_state_source(&s_player_ui);
    const player_command_t command = {
        .kind = PLAYER_COMMAND_STOP_SOURCE,
        .source = AUDIO_SOURCE_NONE,
        .item_index = PLAYER_ITEM_NONE,
    };
    if (!ui_submit_player_command(&command)) return;
    if (old_view != ui_player_state_view(&s_player_ui) ||
        old_source != ui_player_state_source(&s_player_ui)) {
        ui_load_menu_screen();
    }
}

// Re-seeds the list state from a fresh snapshot. Split out because the USB
// browser has to do it again on every directory change, not just on open.
static void ui_reset_list_from_snapshot(const player_snapshot_t *snapshot)
{
    size_t count = snapshot->item_count;
    size_t active_index = snapshot->active_item_index;
    if (s_files_unavailable) {
        // Nothing to list and nothing to scroll: the screen exists only to say
        // why, and to be left with F2 or a long press. The rule marks the
        // bottom of a list that is not there, so it goes with the rows.
        ui_set_label_text_if_changed(s_station_list_title,
                                     ui_source_short_name(s_files_unavailable_source));
        ui_set_label_text_if_changed(
            s_station_list_notice,
            ui_files_notice(s_files_unavailable_source,
                            ui_media_for_source(s_files_unavailable_source),
                            s_last_files_entry_count));
        const lv_image_dsc_t *icon = ui_source_icon(s_files_unavailable_source);
        if (icon != NULL) lv_image_set_src(s_station_list_notice_icon, icon);
        lv_obj_clear_flag(s_station_list_notice_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_station_list_rule, LV_OBJ_FLAG_HIDDEN);
        s_file_browser_has_parent_row = false;
        station_list_init(&s_station_list, 0U, 0U, PLAYER_ITEM_NONE);
        ui_update_station_list();
        return;
    }
    ui_set_label_text_if_changed(s_station_list_notice, "");
    lv_obj_add_flag(s_station_list_notice_icon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_station_list_rule, LV_OBJ_FLAG_HIDDEN);
    if (ui_list_shows_files()) {
        s_file_browser_has_parent_row = !file_browser_path_is_root(snapshot->context);
        const size_t offset = ui_files_row_offset();
        count += offset;
        // The ".." row pushes every entry down, including the playing one.
        active_index = active_index == PLAYER_ITEM_NONE ? PLAYER_ITEM_NONE
                                                        : active_index + offset;
        ui_set_label_text_if_changed(s_station_list_title, ui_path_leaf(snapshot->context));
    } else {
        s_file_browser_has_parent_row = false;
        ui_set_label_text_if_changed(s_station_list_title, "Станции");
    }
    const size_t initial_index =
        station_list_initial_index(count, active_index, ui_files_row_offset());
    station_list_init(&s_station_list, count, initial_index, active_index);
    station_list_note_activity(&s_station_list, ui_tick_get_ms());
    ui_update_station_list();
}

static void ui_load_station_list_screen(void)
{
    player_snapshot_t snapshot;
    player_control_get_snapshot(&snapshot);
    ESP_LOGI(TAG, "show list: source=%d player_state=%d active_item=%u",
             (int)snapshot.active_source, (int)snapshot.playback_state,
             (unsigned int)snapshot.active_item_index);
    s_waiting_for_radio_station = false;
    s_files_listing_revision = player_control_listing_revision();
    ui_reset_list_from_snapshot(&snapshot);
    lv_screen_load(s_station_list_screen);
}

/* Opening the browser should show the file that is playing, not wherever
 * browsing last stopped. Walking out of the playing file's directory and then
 * letting the idle timeout return to the player screen used to leave the
 * listing there for good: the browser reopened on that directory, with no row
 * marked, and the way back to the current album was to walk it again.
 *
 * Posted rather than done here: the listing belongs to the player_control
 * task, and reading a directory from this one would stall the poll loop that
 * drives LVGL. The new listing arrives through the revision the poll already
 * watches, which is the same path a directory change takes. */
static void ui_request_files_reveal(void)
{
    if (!ui_list_shows_files()) return;
    const player_command_t reveal = {
        .kind = PLAYER_COMMAND_BROWSE_REVEAL,
        .source = ui_player_state_source(&s_player_ui),
        .item_index = PLAYER_ITEM_NONE,
    };
    (void)ui_submit_player_command(&reveal);
}

static void ui_show_station_list(void)
{
    // Reachable from the poll loop as well as from a press, so the mode is
    // closed here rather than at each caller.
    ui_end_seek();
    if (!ui_player_state_show_station_list(&s_player_ui)) return;
    s_waiting_for_radio_station = false;
    ui_request_files_reveal();
    ui_load_station_list_screen();
}

/* Leaves the browser for the player, in the view state as well as on screen.
 *
 * Dropping the deferred list-open is the load-bearing part. ui_show_source()
 * arms it for USB and then opens the list itself, and the block that consumes
 * it only runs while the view is SOURCE - which, until a file could move the
 * view out of the list, never happened during browsing. So the request sat
 * armed for the whole session, harmless only because nothing looked at it.
 * The moment the player screen takes over it would fire on the listing
 * revision the user's own directory change produced, throw the player screen
 * away and put the browser back, until the idle timeout undid that too. */
static void ui_leave_station_list(void)
{
    s_files_list_open_requested = false;
    ui_player_state_close_station_list(&s_player_ui);
}

static void ui_close_station_list_to_source(void)
{
    ui_leave_station_list();
    lv_screen_load(s_source_screen);
}

static void ui_render_player_state(void)
{
    switch (ui_player_state_view(&s_player_ui)) {
    case UI_PLAYER_VIEW_MENU:
        ui_load_menu_screen();
        break;
    case UI_PLAYER_VIEW_SOURCE:
        ui_load_source_screen(ui_player_state_source(&s_player_ui));
        break;
    case UI_PLAYER_VIEW_STATION_LIST:
        ui_load_station_list_screen();
        break;
    }
}

/* Grows the progress bar into a control and back. Called only on the two
 * transitions, not per pass: a resize invalidates the area, and this loop runs
 * every 10 ms. */
static void ui_apply_seek_visual(bool seeking)
{
    const int32_t height = seeking ? UI_SRC_PROGRESS_SEEK_H : UI_SRC_PROGRESS_H;
    lv_obj_set_pos(s_source_progress, 10,
                   UI_SRC_PROGRESS_Y - (height - UI_SRC_PROGRESS_H) / 2);
    lv_obj_set_size(s_source_progress, 300, height);
    lv_obj_t *played = lv_obj_get_child(s_source_progress, 0);
    if (played != NULL) lv_obj_set_height(played, height);
}

/* True when the file could be scrubbed right now. The length is the part that
 * can be missing: it is estimated from the first frame's bitrate, so for the
 * first moments of a track - and for the whole of one whose bitrate never
 * became known - there is no scale to aim along. */
static bool ui_seek_available(void)
{
    if (!audio_source_is_files(ui_player_state_source(&s_player_ui))) return false;
    if (ui_player_state_playback(&s_player_ui) != PLAYER_PLAYBACK_PLAYING) return false;
    uint32_t elapsed = 0U;
    uint32_t total = 0U;
    return player_control_track_progress(&elapsed, &total) && total > 0U;
}

/* Opens the mode. Playback is deliberately left running: an earlier version
 * paused the file for the duration, on the theory that hearing one part of the
 * track while pointing at another was confusing - but the break in the music,
 * and the pause badge that came with it, read as a fault rather than as a
 * mode. The track carries on; only the readout follows the knob. */
static void ui_begin_seek(void)
{
    uint32_t elapsed = 0U;
    uint32_t total = 0U;
    if (!player_control_track_progress(&elapsed, &total)) return;
    if (!ui_seek_begin(&s_player_seek, elapsed, total)) return;
    ui_apply_seek_visual(true);
    ui_update_footer();
}

// Leaves scrubbing without jumping. Nothing to undo - the file has been
// playing the whole time - so this is only the target and the bar.
static void ui_end_seek(void)
{
    if (!ui_seek_is_active(&s_player_seek)) return;
    ui_seek_reset(&s_player_seek);
    ui_apply_seek_visual(false);
    ui_update_footer();
}

static void ui_commit_seek(void)
{
    const player_command_t seek = {
        .kind = PLAYER_COMMAND_SEEK,
        .source = ui_player_state_source(&s_player_ui),
        .item_index = PLAYER_ITEM_NONE,
        .position_seconds = ui_seek_target(&s_player_seek),
    };
    (void)ui_submit_player_command(&seek);
    ui_end_seek();
}

static void ui_toggle_playback(void)
{
    const player_command_t command = {
        .kind = PLAYER_COMMAND_TOGGLE,
        .source = AUDIO_SOURCE_NONE,
        .item_index = PLAYER_ITEM_NONE,
    };
    (void)ui_submit_player_command(&command);
}

static void ui_handle_input(board_input_action_t action)
{
    // Settings sits outside the player's view state: it shows no source and
    // starts nothing, so making it a fourth view would put an entry in every
    // transition table for no gain. Any of the three ways out returns to the
    // main screen.
    if (s_settings_open) {
        if (action == BOARD_INPUT_ACTION_F2 || action == BOARD_INPUT_ACTION_ENCODER_LONG) {
            ui_close_settings();
        } else if (action == BOARD_INPUT_ACTION_ENCODER_LEFT ||
                   action == BOARD_INPUT_ACTION_ENCODER_RIGHT) {
            const int direction = action == BOARD_INPUT_ACTION_ENCODER_RIGHT ? 1 : -1;
            (void)ui_settings_model_move(&s_settings_model, direction);
            ui_update_settings();
        } else if (action == BOARD_INPUT_ACTION_ENCODER_BUTTON) {
            const ui_settings_row_t row = ui_settings_model_row_at(
                &s_settings_model, s_settings_model.cursor);
            if (row.kind == UI_SETTINGS_ROW_GROUP) {
                (void)ui_settings_model_activate(&s_settings_model);
            } else {
                ui_settings_change_selected();
            }
            ui_update_settings();
        }
        return;
    }
    if (ui_player_state_view(&s_player_ui) == UI_PLAYER_VIEW_STATION_LIST) {
        station_list_note_activity(&s_station_list, ui_tick_get_ms());
        if (action == BOARD_INPUT_ACTION_F2 ||
            action == BOARD_INPUT_ACTION_ENCODER_LONG) {
            // Same gesture as on the player screen: hold to leave and stop.
            // Without it the list was a dead end for anyone using the encoder
            // alone, since a short press there selects rather than exits.
            ui_show_menu();
        } else if (action == BOARD_INPUT_ACTION_ENCODER_LEFT ||
                   action == BOARD_INPUT_ACTION_ENCODER_RIGHT) {
            if (station_list_handle_input(&s_station_list, action)) ui_update_station_list();
        } else if (action == BOARD_INPUT_ACTION_ENCODER_BUTTON && ui_list_shows_files()) {
            size_t row;
            if (!station_list_get_selection(&s_station_list, &row)) return;
            if (s_file_browser_has_parent_row && row == 0U) {
                const player_command_t up = {
                    .kind = PLAYER_COMMAND_BROWSE_UP,
                    .source = ui_player_state_source(&s_player_ui),
                    .item_index = PLAYER_ITEM_NONE,
                };
                (void)ui_submit_player_command(&up);
                return;
            }
            const size_t index = row - ui_files_row_offset();
            file_browser_entry_t entry;
            if (!player_control_file_entry_at(index, &entry)) return;
            const audio_source_t source = ui_player_state_source(&s_player_ui);
            const player_command_t command = {
                .kind = PLAYER_COMMAND_SELECT_ITEM,
                .source = source,
                .item_index = index,
            };
            if (!ui_submit_player_command(&command)) return;
            // Opening a directory keeps the browser on screen; the new listing
            // arrives through the snapshot poll. Only a file switches to the
            // player.
            if (entry.kind == FILE_BROWSER_ENTRY_DIRECTORY) return;
            // Leave the list in the view state too, not just on screen. The
            // automatic list-to-player transition in
            // ui_player_state_apply_snapshot() only fires for the radio, so USB
            // used to sit in the list view behind the player screen until the
            // 10 s idle timeout - and while it did, the encoder was still bound
            // to "select this row", so a press meant as pause restarted the
            // track and F3 did nothing. Every press also refreshed the idle
            // timer, so pressing again pushed the recovery further away.
            ui_leave_station_list();
            ui_load_source_screen(source);
            lv_label_set_text(s_source_title,
                              ui_menu_item_label(source == AUDIO_SOURCE_SD
                                                     ? UI_MENU_ITEM_SD_CARD
                                                     : UI_MENU_ITEM_USB_FILES));
            ui_set_state_line("Открытие файла", "");
            ui_set_label_text_if_changed(s_source_detail, entry.name);
            ui_set_label_text_if_changed(s_source_stream,
                                         file_browser_format_name(entry.format));
        } else if (action == BOARD_INPUT_ACTION_ENCODER_BUTTON) {
            size_t index;
            if (!station_list_get_selection(&s_station_list, &index) ||
                !ui_player_state_can_select_item(&s_player_ui, index)) return;
            const station_catalog_entry_t *entry = player_control_station_at(index);
            const player_command_t command = {
                .kind = PLAYER_COMMAND_SELECT_ITEM,
                .source = AUDIO_SOURCE_INTERNET_RADIO,
                .item_index = index,
            };
            if (!ui_submit_player_command(&command)) return;
            ui_load_source_screen(AUDIO_SOURCE_INTERNET_RADIO);
            if (entry != NULL) lv_label_set_text(s_source_title, entry->name);
            ui_set_state_line("Connecting", "");
            ui_set_label_text_if_changed(s_source_detail,
                                         entry == NULL ? "" : entry->name);
            char stream_text[32];
            ui_radio_stream_text_for_url(stream_text, sizeof(stream_text),
                                         entry == NULL ? NULL : entry->url);
            ui_set_label_text_if_changed(s_source_stream, stream_text);
        }
        return;
    }
    if (ui_player_state_view(&s_player_ui) == UI_PLAYER_VIEW_SOURCE) {
        const audio_source_t source = ui_player_state_source(&s_player_ui);
        const bool has_list =
            source == AUDIO_SOURCE_INTERNET_RADIO || audio_source_is_files(source);
        if (ui_seek_is_active(&s_player_seek)) {
            // Scrubbing owns the knob and the press while it is open, so the
            // volume and the play/pause click are unreachable and cannot be
            // triggered by accident by the gesture that chooses a position.
            if (action == BOARD_INPUT_ACTION_ENCODER_LEFT ||
                action == BOARD_INPUT_ACTION_ENCODER_RIGHT) {
                if (ui_seek_move(&s_player_seek,
                                 action == BOARD_INPUT_ACTION_ENCODER_RIGHT ? 1 : -1)) {
                    ui_update_footer();
                }
                return;
            }
            if (action == BOARD_INPUT_ACTION_ENCODER_BUTTON) {
                ui_commit_seek();
                return;
            }
            /* Every other button abandons the scrub rather than doing its own
             * job on top of it: a mode with only one way out is a trap. The
             * two that leave the screen go on to do that as well, since
             * closing the mode changes nothing about what is playing. */
            ui_end_seek();
            if (action != BOARD_INPUT_ACTION_F2 &&
                action != BOARD_INPUT_ACTION_ENCODER_LONG) {
                return;
            }
        }
        if (action == BOARD_INPUT_ACTION_F2 ||
            action == BOARD_INPUT_ACTION_ENCODER_LONG) {
            // Leaving the screen entirely, so a click waiting out its window
            // must not land on the home screen.
            ui_click_gesture_cancel(&s_player_click);
            ui_show_menu();
        } else if (has_list && action == BOARD_INPUT_ACTION_ENCODER_BUTTON) {
            // Double click opens the list, triple click starts scrubbing, and
            // both leave playback alone; the single click that toggles
            // play/pause is delivered later, from the poll loop, once no
            // further press has arrived.
            switch (ui_click_gesture_press(&s_player_click, ui_tick_get_ms(),
                                           ui_seek_available())) {
            case UI_CLICK_DOUBLE:
                ui_show_station_list();
                break;
            case UI_CLICK_TRIPLE:
                ui_begin_seek();
                break;
            default:
                break;
            }
        } else if (action == BOARD_INPUT_ACTION_ENCODER_LEFT ||
                   action == BOARD_INPUT_ACTION_ENCODER_RIGHT) {
            // The encoder was unused on this screen, which is why volume gets
            // it: no gesture has to be given up to make room.
            const int step = action == BOARD_INPUT_ACTION_ENCODER_RIGHT
                                 ? UI_VOLUME_STEP_PERCENT
                                 : -UI_VOLUME_STEP_PERCENT;
            const uint8_t volume = audio_volume_step(board_audio_volume(), step);
            // Reaches the audio path at once - it is one atomic store, and the
            // next PCM block is already attenuated. Saving is what has to wait:
            // writing settings.csv per click stalled this task long enough that
            // clicks queued up and then arrived in a burst.
            board_audio_set_volume(volume);
            s_volume_save_pending = true;
            s_volume_changed_ms = ui_tick_get_ms();
            ui_update_footer();
        } else if (has_list && action == BOARD_INPUT_ACTION_F3) {
            ui_toggle_playback();
        }
        return;
    }
    if (s_device_settings.home_screen == DEVICE_HOME_SCREEN_FEED &&
        lv_screen_active() == s_feed_screen) {
        if (action == BOARD_INPUT_ACTION_ENCODER_LEFT ||
            action == BOARD_INPUT_ACTION_ENCODER_RIGHT) {
            /*
             * The encoder's physical clockwise direction is opposite to the
             * visual feed order on this board.  Keep the common menu semantics
             * unchanged and reverse movement only for the horizontal feed.
             */
            ui_feed_model_move(&s_feed_model,
                               action == BOARD_INPUT_ACTION_ENCODER_RIGHT ? -1 : 1);
            ui_update_feed_screen();
        } else if (action == BOARD_INPUT_ACTION_ENCODER_BUTTON) {
            const ui_feed_item_t item = ui_feed_model_selected(&s_feed_model);
            if (item == UI_FEED_SETTINGS) {
                ui_show_settings();
            } else {
                audio_source_t source = AUDIO_SOURCE_NONE;
                if (!ui_feed_model_activate(item, &source)) {
                    ui_set_label_text_if_changed(s_feed_notice, "Функция пока недоступна");
                    return;
                }
                ui_set_label_text_if_changed(s_feed_notice, "");
                (void)ui_menu_select_source(&s_menu, source);
                ui_show_source();
            }
        } else if (action == BOARD_INPUT_ACTION_F2) {
            ui_show_menu();
        }
        return;
    }
    if (ui_menu_handle_input(&s_menu, action)) {
        // Clears the "insert a drive" notice as soon as the user moves on.
        ui_set_label_text_if_changed(s_menu_notice, "");
        ui_update_menu_highlight();
    } else if (action == BOARD_INPUT_ACTION_ENCODER_BUTTON) {
        if (ui_menu_selection_is_settings(&s_menu)) {
            ui_show_settings();
            return;
        }
        ui_show_source();
    }
}

/* Keeps the resume point current. Written from the poll loop rather than at
 * the moment of selection because this is the one place that sees both the
 * active source and the file the USB player actually opened; the setters skip
 * a write when nothing changed, so this does not hammer the flash. */
static void ui_remember_playing(const player_snapshot_t *snapshot)
{
    if (!s_device_settings.autoplay) return;
    switch (snapshot->active_source) {
    case AUDIO_SOURCE_INTERNET_RADIO:
        (void)device_settings_set_last_source(&s_device_settings,
                                              DEVICE_LAST_SOURCE_INTERNET_RADIO);
        return;
    case AUDIO_SOURCE_SD:
    case AUDIO_SOURCE_USB: {
        (void)device_settings_set_last_source(&s_device_settings,
                                              snapshot->active_source == AUDIO_SOURCE_SD
                                                  ? DEVICE_LAST_SOURCE_SD
                                                  : DEVICE_LAST_SOURCE_USB);
        /* Asked of the controller rather than built from the snapshot's
         * directory and track name: browsing moves that directory while the
         * track plays on, and the two together then name a file nobody opened.
         *
         * Returning false covers "nothing is playing" as well, which is the
         * old guard: leaving the browser must not erase the previous resume
         * point. */
        char path[FILE_BROWSER_PATH_MAX_LEN];
        if (player_control_playing_file_path(path, sizeof(path))) {
            (void)device_settings_set_last_file(&s_device_settings, path);
        }
        return;
    }
    default:
        return;
    }
}

static void ui_sync_player_snapshot(const player_snapshot_t *snapshot)
{
    if (snapshot == NULL) return;
    const ui_player_view_t old_view = ui_player_state_view(&s_player_ui);
    const audio_source_t old_source = ui_player_state_source(&s_player_ui);
    s_last_usb_media = snapshot->usb_media;
    s_last_sd_media = snapshot->sd_media;
    s_last_files_entry_count = snapshot->files_entry_count;
    ui_remember_playing(snapshot);
    if (s_files_unavailable &&
        ui_files_can_open(s_files_unavailable_source,
                          ui_media_for_source(s_files_unavailable_source),
                          s_last_files_entry_count)) {
        // A drive appeared while its "unavailable" screen was open: pick the
        // source up rather than making the user back out and re-enter.
        const player_command_t select = {
            .kind = PLAYER_COMMAND_SELECT_SOURCE,
            .source = s_files_unavailable_source,
            .item_index = PLAYER_ITEM_NONE,
        };
        s_files_unavailable = false;
        s_files_unavailable_source = AUDIO_SOURCE_NONE;
        if (ui_submit_player_command(&select)) {
            s_files_list_open_revision = player_control_listing_revision();
            s_files_list_open_requested = true;
        }
    }
    /* The file can stop under the mode - it ended, it failed, the drive was
     * pulled - and then there is nothing left to scrub. Paused is not one of
     * those: it can only arrive from the web UI while this screen is open, and
     * the position it stopped at is still worth aiming at. */
    if (ui_seek_is_active(&s_player_seek) &&
        (!audio_source_is_files(snapshot->active_source) ||
         snapshot->playback_state == PLAYER_PLAYBACK_STOPPED ||
         snapshot->playback_state == PLAYER_PLAYBACK_ERROR)) {
        ui_end_seek();
    }
    ui_player_state_apply_snapshot(&s_player_ui, snapshot, ui_tick_get_ms());
    if (old_view != ui_player_state_view(&s_player_ui) ||
        old_source != ui_player_state_source(&s_player_ui)) {
        // The player screen went away on its own - a station finished
        // connecting, the source stopped - so a click waiting on it is stale.
        ui_click_gesture_cancel(&s_player_click);
        ui_render_player_state();
    }

    if (s_files_list_open_requested &&
        audio_source_is_files(ui_player_state_source(&s_player_ui)) &&
        ui_player_state_view(&s_player_ui) == UI_PLAYER_VIEW_SOURCE &&
        player_control_listing_revision() != s_files_list_open_revision) {
        s_files_list_open_requested = false;
        ui_show_station_list();
    }

    if (s_waiting_for_radio_station &&
        snapshot->active_source == AUDIO_SOURCE_INTERNET_RADIO &&
        snapshot->active_item_index == PLAYER_ITEM_NONE &&
        snapshot->playback_state == PLAYER_PLAYBACK_STOPPED &&
        (uint32_t)(ui_tick_get_ms() - s_radio_station_wait_started_ms) >=
            UI_RADIO_EMPTY_LIST_DELAY_MS) {
        ui_show_station_list();
    }

    if (ui_player_state_view(&s_player_ui) == UI_PLAYER_VIEW_STATION_LIST) {
        const unsigned int revision = player_control_listing_revision();
        if (ui_list_shows_files() && !s_files_unavailable &&
            !ui_files_media_present(
                ui_media_for_source(ui_player_state_source(&s_player_ui)))) {
            /* The drive left while its own listing was on screen. Nothing
             * refills the rows afterwards - the listing simply empties - so
             * without this the browser stays up as a blank screen. */
            s_files_unavailable = true;
            s_files_unavailable_source = ui_player_state_source(&s_player_ui);
            s_files_listing_revision = revision;
            ui_reset_list_from_snapshot(snapshot);
        } else if (ui_list_shows_files() && revision != s_files_listing_revision) {
            // A directory was opened or left: the rows now describe different
            // files, so the cursor cannot keep its position.
            s_files_listing_revision = revision;
            ui_reset_list_from_snapshot(snapshot);
        } else if (!s_files_unavailable &&
                   station_list_sync_counts(&s_station_list,
                                            snapshot->item_count + ui_files_row_offset(),
                                            snapshot->active_item_index == PLAYER_ITEM_NONE
                                                ? PLAYER_ITEM_NONE
                                                : snapshot->active_item_index +
                                                      ui_files_row_offset())) {
            /* The playlist can be replaced from the web UI while this screen is
             * open, so the count captured at open time may be stale.
             *
             * Never on the notice screen, and that is the load-bearing part:
             * no source is selected there, so the snapshot still describes the
             * radio catalogue. Syncing to its count filled the screen with as
             * many blank rows as there are stations, cursor bar and all, and
             * drew them over the message the screen exists for. */
            ui_update_station_list();
        }
        // The timeout exists to return to something worth looking at. With
        // nothing playing there is nothing behind this screen, so browsing is
        // allowed to take as long as the user wants.
        const bool playback_running =
            snapshot->playback_state == PLAYER_PLAYBACK_PLAYING ||
            snapshot->playback_state == PLAYER_PLAYBACK_PAUSED ||
            snapshot->playback_state == PLAYER_PLAYBACK_CONNECTING ||
            snapshot->playback_state == PLAYER_PLAYBACK_RECONNECTING;
        if (playback_running &&
            station_list_idle_timeout_elapsed(&s_station_list, ui_tick_get_ms(),
                                              UI_STATION_LIST_IDLE_TIMEOUT_MS)) {
            ui_close_station_list_to_source();
        }
    }
    ui_update_radio_status(snapshot);
}

static void ui_autoplay_step(const player_snapshot_t *snapshot)
{
    if (!s_autoplay_pending) return;
    const ui_autoplay_action_t action =
        ui_autoplay_decide(&s_device_settings, snapshot->usb_media, snapshot->sd_media,
                           false);
    const bool waited =
        (uint32_t)(ui_tick_get_ms() - s_autoplay_started_ms) >= UI_AUTOPLAY_FILE_WAIT_MS;
    // Hold off only while the answer could still change: a drive that has not
    // shown up yet may still mount.
    if (action == UI_AUTOPLAY_FILE_UNAVAILABLE && !waited) return;
    s_autoplay_pending = false;

    switch (action) {
    case UI_AUTOPLAY_RADIO: {
        // Not ui_show_source(): that opens the list, which is right when the
        // user is picking something and wrong here - autoplay already knows
        // what plays, so the player screen is the answer.
        (void)ui_menu_select_source(&s_menu, AUDIO_SOURCE_INTERNET_RADIO);
        const player_command_t select = {
            .kind = PLAYER_COMMAND_SELECT_SOURCE,
            .source = AUDIO_SOURCE_INTERNET_RADIO,
            .item_index = PLAYER_ITEM_NONE,
        };
        if (!ui_submit_player_command(&select)) return;
        /* Selecting the source only opens it; starting the last station is a
         * separate ask, and autoplay is the only thing that makes it. The two
         * are queued in order, so the start lands on a snapshot that already
         * names the radio as the active source. */
        const player_command_t play = {
            .kind = PLAYER_COMMAND_PLAY,
            .source = AUDIO_SOURCE_INTERNET_RADIO,
            .item_index = PLAYER_ITEM_NONE,
        };
        (void)ui_submit_player_command(&play);
        ui_load_source_screen(AUDIO_SOURCE_INTERNET_RADIO);
        // That screen arms the "no station to play" fallback, which opens the
        // list after a moment. Autoplay has a station, so the only thing that
        // fallback could do here is flip away from the screen just loaded.
        s_waiting_for_radio_station = false;
        return;
    }
    case UI_AUTOPLAY_FILE_UNAVAILABLE:
        (void)ui_menu_select_source(&s_menu, ui_autoplay_source(&s_device_settings));
        ui_show_source();
        return;
    case UI_AUTOPLAY_FILE:
    case UI_AUTOPLAY_FILE_BROWSER: {
        const audio_source_t source = ui_autoplay_source(&s_device_settings);
        const player_command_t select = {
            .kind = PLAYER_COMMAND_SELECT_SOURCE,
            .source = source,
            .item_index = PLAYER_ITEM_NONE,
        };
        if (!ui_submit_player_command(&select)) return;
        // Whether the remembered file is still there is only knowable by
        // looking, so the attempt itself is the test: it opens the file's
        // directory either way, leaving the browser somewhere useful.
        if (player_control_file_resume_path(s_device_settings.last_file)) {
            ui_load_source_screen(source);
            return;
        }
        s_files_list_open_revision = player_control_listing_revision();
        s_files_list_open_requested = true;
        ui_show_station_list();
        return;
    }
    case UI_AUTOPLAY_HOME:
    default:
        return;
    }
}

/* Runs every poll, not only when something changed: the meter is an animation,
 * and its whole job is to keep moving between PCM blocks - 26 ms of MP3, 93 ms
 * of FLAC, against a 10 ms loop. */
static void ui_update_vu(void)
{
    if (lv_screen_active() != s_source_screen) return;
    const uint32_t now = ui_tick_get_ms();
    const uint32_t elapsed = now - s_vu_updated_ms;
    s_vu_updated_ms = now;

    uint16_t peak[2] = {0U, 0U};
    const bool fresh = board_audio_level_take(&peak[0], &peak[1]);
    for (size_t channel = 0; channel < 2U; ++channel) {
        const uint8_t value =
            ui_vu_meter_advance(&s_vu_state[channel], fresh, peak[channel], elapsed);
        const uint8_t lit = ui_vu_lit_segments(value, UI_VU_SEGMENTS);
        for (uint8_t segment = 0U; segment < UI_VU_SEGMENTS; ++segment) {
            // Unlit blocks stay visible in a dim shade rather than hiding, so
            // the meter reads as a scale at rest instead of an empty strip.
            const uint32_t colour =
                segment >= lit ? 0x263746U
                : ui_vu_segment_is_red(segment, UI_VU_SEGMENTS) ? 0xE53935U
                                                                : 0x43A047U;
            /* Only touch a block whose colour actually changed.
             *
             * lv_obj_set_style_bg_color() does not compare against the current
             * value - it refreshes the style and invalidates the object every
             * time. Forty blocks meant forty invalid areas per pass against
             * LVGL's LV_INV_BUF_SIZE of 32, and on overflow it throws the list
             * away and repaints the whole display instead. That is what made
             * every pass repaint all 76800 pixels and took the loop from the
             * ~10 ms it is meant to run at to 222 ms: input arrived in bursts,
             * and the meter itself updated four times a second. In steady state
             * only a block or two changes here. */
            if (s_vu_colour[channel][segment] == colour) continue;
            s_vu_colour[channel][segment] = colour;
            lv_obj_set_style_bg_color(s_source_vu[channel][segment],
                                      lv_color_hex(colour), 0);
        }
    }
}

static void ui_task(void *arg)
{
    (void)arg;
    while (true) {
        board_input_action_t action;
        if (xQueueReceive(s_input_queue, &action, pdMS_TO_TICKS(10)) == pdTRUE) {
            ui_handle_input(action);
            /* Drain whatever else is already queued before rendering.
             *
             * Taking one event per pass capped input at one event per loop
             * iteration, and an iteration also redraws the meter and runs
             * lv_timer_handler(). The encoder is polled every 5 ms, so a quick
             * turn produced events far faster than that cap and they drained
             * afterwards - the knob appeared to keep turning by itself. There
             * is nothing to gain from a redraw between two clicks of the same
             * gesture; the queue holds 16, so this cannot spin for long. */
            while (xQueueReceive(s_input_queue, &action, 0) == pdTRUE) {
                ui_handle_input(action);
            }
        }
        switch (ui_click_gesture_poll(&s_player_click, ui_tick_get_ms())) {
        case UI_CLICK_SINGLE:
            ui_toggle_playback();
            break;
        // Only reachable where a third press was worth waiting for: with
        // scrubbing available the double has to outlast its own window before
        // it can be told from the start of a triple.
        case UI_CLICK_DOUBLE:
            ui_show_station_list();
            break;
        default:
            break;
        }
        if (ui_volume_commit_due(s_volume_save_pending, s_volume_changed_ms,
                                 ui_tick_get_ms(), UI_VOLUME_SETTLE_MS)) {
            s_volume_save_pending = false;
            (void)device_settings_set_volume(&s_device_settings, board_audio_volume());
        }
        player_snapshot_t snapshot;
        player_control_get_snapshot(&snapshot);
        ui_autoplay_step(&snapshot);
        // Only the strip on screen: the others are on parents LVGL is not
        // drawing, so writing them would be work for nothing.
        {
            lv_obj_t *active = lv_screen_active();
            ui_status_strip_t *strip = active == s_source_screen         ? &s_source_strip
                                       : active == s_menu_screen         ? &s_menu_strip
                                       : active == s_feed_screen         ? &s_feed_strip
                                       : active == s_station_list_screen ? &s_list_strip
                                                                         : NULL;
            ui_status_strip_update(strip, &snapshot);
        }
        ui_update_vu();
        ui_update_footer();
        ui_update_cover();
        if (s_settings_open) {
            ui_update_settings();
        } else {
            ui_sync_player_snapshot(&snapshot);
        }
        /* Driven from here because this is the one task that is always
         * running and has stack to spare; it reports on its own schedule and
         * is a few comparisons in between. */
        system_report_tick(ui_tick_get_ms());
        /* A repaint this slow is a fault, not a slow frame: the loop is meant
         * to run in tens of milliseconds, and the display task holds a core
         * while it draws. Kept because the fault that motivated it - LVGL
         * looping forever inside one call - was invisible from the outside
         * except as a frozen screen. */
        {
            const int64_t started = esp_timer_get_time();
            lv_timer_handler();
            const int64_t took = (esp_timer_get_time() - started) / 1000;
            // A screen change legitimately repaints all 76800 pixels and costs
            // about 200 ms, so the threshold sits well above that: what this
            // is watching for is a pass that never ends.
            if (took > 400) {
                lv_obj_t *active = lv_screen_active();
                ESP_LOGW(TAG, "slow repaint: %dms on %s", (int)took,
                         active == s_source_screen    ? "player"
                         : active == s_feed_screen    ? "feed"
                         : active == s_menu_screen    ? "menu"
                         : active == s_settings_screen ? "settings"
                         : active == s_station_list_screen ? "list"
                                                       : "other");
            }
        }
    }
}

esp_err_t ui_init(void)
{
    if (s_input_queue != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "init memory: internal_free=%u internal_largest=%u dma_largest=%u draw_buffer=%u",
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL),
             (unsigned int)UI_DRAW_BUFFER_SIZE);
    s_input_queue = xQueueCreate(UI_INPUT_QUEUE_LENGTH, sizeof(board_input_action_t));
    if (s_input_queue == NULL) {
        ESP_LOGE(TAG, "input queue allocation failed");
        return ESP_ERR_NO_MEM;
    }
    ui_menu_init(&s_menu);
    ui_player_state_init(&s_player_ui);
    ui_click_gesture_init(&s_player_click);
    ui_vu_meter_init(&s_vu_state[0]);
    ui_vu_meter_init(&s_vu_state[1]);
    s_vu_updated_ms = ui_tick_get_ms();
    s_waiting_for_radio_station = false;
    /* PSRAM, and not fatal if it fails: without it the tile keeps showing its
     * placeholder and everything else on the screen still works. */
    s_source_cover_pixels = heap_caps_malloc(ALBUM_ART_PIXELS * sizeof(uint16_t),
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_source_cover_pixels == NULL) {
        ESP_LOGW(TAG, "no memory for the cover; the placeholder tile stays");
    }
    lv_init();
    lv_tick_set_cb(ui_tick_get_ms);

    lv_color_t *buffer1 = heap_caps_malloc(UI_DRAW_BUFFER_SIZE,
                                           MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (buffer1 == NULL) {
        ESP_LOGE(TAG, "draw buffer allocation failed: buffer=%p dma_largest=%u",
                 buffer1,
                 (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
        heap_caps_free(buffer1);
        vQueueDelete(s_input_queue);
        s_input_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_display = lv_display_create(TFT_WIDTH, TFT_HEIGHT);
    if (s_display == NULL) {
        ESP_LOGE(TAG, "LVGL display allocation failed: internal_largest=%u",
                 (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        heap_caps_free(buffer1);
        vQueueDelete(s_input_queue);
        s_input_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    lv_display_set_color_format(s_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(s_display, buffer1, NULL, UI_DRAW_BUFFER_SIZE,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_display, ui_flush);
    ui_create_menu_screen();
    ui_create_feed_screen();
    ui_create_settings_screen();
    ui_create_source_screen();
    ui_create_station_list_screen();
    ui_settings_model_init(&s_settings_model);
    if (!device_settings_init(&s_device_settings)) {
        lv_label_set_text(s_settings_notice, "Ошибка чтения settings.csv");
    } else {
        (void)board_display_set_rotation(s_device_settings.flip_vertical,
                                         s_device_settings.flip_horizontal);
    }
    // Before anything can play: the board defaults to full volume, and coming
    // back from a power cut at full blast when the user had it at 20 is the
    // kind of surprise a saved setting exists to prevent.
    board_audio_set_volume(s_device_settings.volume);
    // Asked with both volumes assumed ready: this only decides whether there is
    // anything to wait for at all. What is actually there is settled later, by
    // ui_autoplay_step(), once the drive has had time to enumerate.
    s_autoplay_pending = ui_autoplay_decide(&s_device_settings, FILE_BROWSER_MEDIA_READY,
                                            FILE_BROWSER_MEDIA_READY, true) !=
                         UI_AUTOPLAY_HOME;
    s_autoplay_started_ms = ui_tick_get_ms();
    if (s_device_settings.home_screen == DEVICE_HOME_SCREEN_FEED) {
        lv_screen_load(s_feed_screen);
    } else {
        lv_screen_load(s_menu_screen);
    }

    // Pinned to core 0 alongside Wi-Fi and lwIP: LVGL rendering and the
    // synchronous SPI flush are not time critical, but they are long enough to
    // stall the audio decoder pinned to core 1 if the scheduler puts them
    // there.
    if (xTaskCreatePinnedToCore(ui_task, "ui", UI_TASK_STACK_SIZE, NULL, UI_TASK_PRIORITY,
                                NULL, 0) != pdPASS) {
        ESP_LOGE(TAG, "UI task allocation failed: internal_free=%u internal_largest=%u",
                 (unsigned int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        lv_display_delete(s_display);
        s_display = NULL;
        heap_caps_free(buffer1);
        vQueueDelete(s_input_queue);
        s_input_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "LVGL UI initialized");
    return ESP_OK;
}

bool ui_post_input(board_input_action_t action)
{
    if (s_input_queue == NULL || action == BOARD_INPUT_ACTION_NONE) {
        return false;
    }
    if (xQueueSend(s_input_queue, &action, 0) != pdTRUE) {
        ESP_LOGW(TAG, "input queue full; action=%d dropped", (int)action);
        return false;
    }
    return true;
}
