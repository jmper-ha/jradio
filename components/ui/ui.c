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
#include "ui_buffer_graph.h"
#include "ui_click_gesture.h"
#include "ui_draw_buffer.h"
#include "ui_fonts.h"
#include "ui_layout.h"
#include "ui_menu.h"
#include "ui_feed_icon_bitmaps.h"
#include "ui_feed_icons.h"
#include "ui_feed_model.h"
#include "ui_player_state.h"
#include "ui_now_playing.h"
#include "ui_radio_text.h"
#include "ui_seek.h"
#include "ui_settings_model.h"
#include "ui_station_list.h"
#include "ui_text_scroll.h"
#include "ui_status_bar.h"
#include "ui_web_address.h"
#include "ui_yandex_screen.h"
#include "yandex_catalog.h"
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
/* A row that is on the screen but cannot be started: dimmer than the
 * unselected text, still plainly readable against the ground. */
#define UI_COLOR_DISABLED 0x4E606C
/* Only ever a warning; never decoration, so it stays out of the ramp above. */
#define UI_COLOR_NOTICE 0xFFD54F
/* A state that is a failure rather than a step: "Connection error" and nothing
 * else so far. Red because it is the one line on the player screen the user is
 * meant to stop at, and in plain text it read like "Connecting..." at a
 * glance. */
#define UI_COLOR_ERROR 0xEF5350
/* Folder rows in the USB browser. Deliberately duller than the accent, which
 * means "this is the one playing" - a folder is a place, not a state. */
#define UI_COLOR_FOLDER 0xC08A1E
/* A raised step rather than a colour of its own: the row under the cursor is
 * lifted off the ground and its text takes the accent, which is how the player
 * screen marks the thing being played. */
#define UI_COLOR_SELECTED 0x2A3B4A
/* The settings screen needs a brighter cursor than the menu's. Its rows are
 * already tinted tiles rather than bare background, and UI_COLOR_SELECTED
 * lands within a few percent of the tile under it - close enough that the
 * cursor could not be found without moving it. This is well clear of both. */
#define UI_COLOR_CURSOR 0x3F6187

/* Depth is carried by brightness as well as size: without it the middle icon
 * reads as the only lit one rather than as the middle of a ring. */
#define UI_COLOR_FEED_NEAR 0x8FA8BC
#define UI_COLOR_FEED_FAR 0x46586A
/* The dots under the carousel, dimmer again than the far icons. */
#define UI_COLOR_FEED_DOT 0x33445A

/* The code covers the settings screen, and nothing on it moves - so unlike a
 * list there is no activity to measure, only how long it has been up. Long
 * enough for a phone to be fetched from another room, short enough that the
 * screen does not sit on a QR code all evening. */
#define UI_QR_IDLE_TIMEOUT_MS 30000U
/* How long a list waits with nothing on it before giving up and going back,
 * and how long an idle station list stays open. */
#define UI_RADIO_EMPTY_LIST_DELAY_MS 250U
#define UI_STATION_LIST_IDLE_TIMEOUT_MS 10000U

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
static lv_obj_t *s_feed_icons[UI_FEED_SLOTS];
static lv_obj_t *s_feed_dots[UI_FEED_ITEM_COUNT];
static ui_feed_model_t s_feed_model;
static lv_obj_t *s_source_title;
static lv_obj_t *s_source_status;
/* A line that scrolls when it does not fit.
 *
 * Two objects rather than one, because an LVGL label clips its text to its own
 * area: moving the label to scroll the text moves the clip with it, and the
 * text spills across the screen. So the box stays put and clips, and a label
 * inside it does the travelling. The box carries the styling - background,
 * radius, padding - and LVGL inherits text colour and font from it, so callers
 * style the box exactly as they styled the label it replaced. */
typedef struct {
    lv_obj_t *box;
    lv_obj_t *text;
    /* When the current line appeared, so the animation is a function of the
     * clock rather than of how many frames have been drawn. */
    uint32_t started_ms;
    int32_t applied_x;
    bool scrolling;
    /* Where a line that fits comes to rest. List rows leave it false and start
     * at the left margin; the player's column in portrait centres, or a short
     * track name would sit against the margin under a centred cover while the
     * three plain labels around it were centred. */
    bool centred;
} ui_scroller_t;

static ui_scroller_t s_source_detail;
static lv_obj_t *s_source_stream;
static lv_obj_t *s_source_buffer;
/* The same reading as a strip: one object, whose bars are drawn into it from
 * the model below rather than being objects of their own. Both faces exist for
 * the life of the screen and one of them is hidden - building either on demand
 * would mean allocating on the poll loop, and the setting can change while the
 * player is on screen. */
static lv_obj_t *s_source_buffer_graph;
static ui_buffer_graph_t s_buffer_graph;
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
static ui_status_strip_t s_yandex_strip;
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
/* The like mark, between the buffer reading and the volume. Only the rotor's
 * tracks have one, so it is hidden for every other source rather than shown
 * empty - an empty heart on a radio station would offer something the button
 * cannot do there. */
static lv_obj_t *s_source_like;
/* What the heart is currently drawing: -1 nothing, 0 empty, 1 filled. Swapping
 * the bitmap invalidates the area, and this is decided on every pass of a
 * 10 ms loop while the mark changes about once a track. */
static int8_t s_source_like_shown = -1;
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
/* Same deal as the volume, and for the same reason: the panel follows the knob
 * on the very next detent, settings.csv follows once it stops turning. */
#define UI_BRIGHTNESS_SETTLE_MS 1500U

static lv_obj_t *s_source_vu[2][UI_VU_SEGMENTS];
/* Colour last written to each block, so an unchanged one is left alone; see
 * ui_update_vu() for why that matters so much. 0 is not a colour any block
 * takes, so the first pass always paints. */
static uint32_t s_vu_colour[2][UI_VU_SEGMENTS];
static ui_vu_meter_t s_vu_state[2];
static uint32_t s_vu_updated_ms;
static lv_obj_t *s_settings_screen;
static lv_obj_t *s_settings_rows[UI_SETTINGS_MAX_ROWS];
static lv_obj_t *s_settings_more_above;
static lv_obj_t *s_settings_more_below;
/* One per boolean setting, not per row on screen: only one group is open at
 * a time, so at most three are ever visible, but each keeps its own object. */
#define UI_SETTINGS_SWITCH_COUNT 4U
static lv_obj_t *s_settings_switches[UI_SETTINGS_SWITCH_COUNT];
static lv_obj_t *s_settings_web_band;
static lv_obj_t *s_settings_web_address;
static lv_obj_t *s_settings_web_hint;
static lv_obj_t *s_qr_overlay;
static lv_obj_t *s_qr_code;
static lv_obj_t *s_qr_caption;
static lv_obj_t *s_qr_back;
static bool s_qr_open;
static uint32_t s_qr_opened_ms;
/* What the code on screen currently encodes, so it is rebuilt only when the
 * payload actually changes: the settings screen updates on every pass of the
 * UI loop, and a rebuild is a full re-encode plus a canvas repaint. */
static char s_qr_shown[128];
static lv_obj_t *s_settings_notice;
static ui_settings_model_t s_settings_model;
static device_settings_t s_device_settings;
static bool s_settings_open;
/* Set when the radio could not be opened at once on a device with no home
 * screen - see ui_open_radio_home(). Retried from the poll loop, which is
 * where the pending command it is waiting on gets resolved. */
static bool s_radio_home_pending;
static lv_obj_t *s_yandex_screen;
static lv_obj_t *s_yandex_status;
static lv_obj_t *s_yandex_code_panel;
static lv_obj_t *s_yandex_code;
static lv_obj_t *s_yandex_url;
static lv_obj_t *s_yandex_countdown;
static lv_obj_t *s_yandex_hint;
static bool s_yandex_open;
/* The row waiting to be started, and when to give up on it. Long enough to
 * cover selecting a source, which stops whatever was playing and can block for
 * seconds while a decoder task exits. */
#define UI_YANDEX_START_TIMEOUT_MS 15000U
static size_t s_yandex_start_row = PLAYER_ITEM_NONE;
static uint32_t s_yandex_start_deadline_ms;
/* The station rows are the list screen's rows, built from the same constants
 * rather than from a set of their own: this screen is a third list, and the
 * geometry it had - a shorter row at a tighter pitch, no rounded corner - was
 * the whole of why it did not look like one.
 *
 * The hint line the old pitch was making room for is gone in list mode; the
 * rule and the position bar take that space, the way they do on the other two
 * lists. */
#define UI_YANDEX_LIST_ROWS UI_STATION_LIST_MAX_ROWS
/* Long enough to read, short enough that the button hints come back before
 * the next press. */
#define UI_YANDEX_NOTICE_MS 3000U
static ui_scroller_t s_yandex_rows[UI_YANDEX_LIST_ROWS];
static lv_obj_t *s_yandex_rule;
static lv_obj_t *s_yandex_progress;
/* The "nothing to list" screen: the same drive-and-a-sentence shape the file
 * browser uses when there is no disc, with the Yandex mark in place of the
 * drive. It replaces a grey 14-pixel line in the top corner, which read as a
 * caption rather than as the message the screen existed for. */
static lv_obj_t *s_yandex_message_icon;
static lv_obj_t *s_yandex_message;
static station_list_state_t s_yandex_list;
/* What the rows were last drawn from. Setting a label's text restarts its
 * scroll animation, so the rows are redrawn only when one of these moved -
 * never on every pass of the poll loop. */
static unsigned int s_yandex_drawn_revision;
static size_t s_yandex_drawn_selected;
static size_t s_yandex_drawn_count;
static size_t s_yandex_drawn_active;
static bool s_yandex_rows_drawn;
/* What the last pass decided the screen is showing. The idle timer only
 * applies to the list - a pairing code takes a minute to type on a phone, and
 * closing that screen out from under someone doing it would be a fault. */
static ui_yandex_mode_t s_yandex_mode = UI_YANDEX_MODE_PAIRING;
static uint32_t s_yandex_notice_until_ms;
static const char *s_yandex_notice;
static lv_obj_t *s_station_list_screen;
static lv_obj_t *s_station_list_title;
static lv_obj_t *s_station_list_notice;
/* The notice replaces the list rather than sitting above it, so the two parts
 * that only make sense with rows behind them - the rule and the drive picture
 * that stands in for them - are held to be shown and hidden together with it. */
static lv_obj_t *s_station_list_notice_icon;
static lv_obj_t *s_station_list_rule;
static ui_scroller_t s_station_list_rows[UI_STATION_LIST_MAX_ROWS];
/* The folder mark is a label of its own rather than a glyph inside the row's
 * text: inline it would take the row's font and colour, and it has to be
 * bigger than the name beside it and in its own colour. */
static lv_obj_t *s_station_list_icons[UI_STATION_LIST_MAX_ROWS];
/* The index down the left of a station row. Its own label rather than part of
 * the row text: the row under the cursor scrolls, and the number is what the
 * eye counts down - it has to stay where it is while the name travels. */
static lv_obj_t *s_station_list_numbers[UI_STATION_LIST_MAX_ROWS];
static lv_obj_t *s_station_list_progress;
static station_list_state_t s_station_list;
static ui_player_state_t s_player_ui;
// Only the player screen tells a single click from a double one, so only
// there does a click wait out the window; every other screen acts at once.
static ui_click_gesture_t s_player_click;
/* The like key has its own gesture, because it is its own button: a press of
 * it must not join a gesture the encoder started, and both can be pending at
 * once. Only the rotor arms it - everywhere else BTN_PREV steps a list and a
 * 350 ms wait for a second press would be felt. */
static ui_click_gesture_t s_like_click;
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
static bool s_last_wifi_connected;
static bool s_last_wifi_setup_ap;
static file_browser_media_t s_last_usb_media = FILE_BROWSER_MEDIA_ABSENT;
static file_browser_media_t s_last_sd_media = FILE_BROWSER_MEDIA_ABSENT;
static size_t s_last_files_entry_count;
/* Which source the "nothing to browse" screen is explaining. Without it the
 * notice would name a USB drive while the user was asking for the card, and
 * the pick-up below would select the wrong source when a volume appeared. */
static audio_source_t s_files_unavailable_source = AUDIO_SOURCE_NONE;
// Autoplay runs once, and only after what it depends on has had time to
// appear: deciding "no drive" or "no network" any earlier would be deciding it
// before the answer exists. Both are caps and not only waits - a device that
// will never get either has to stop waiting and show something.
//
// A drive mounts around five seconds in, later still when the root port needs
// re-enumerating, so twelve seconds covers it.
#define UI_AUTOPLAY_WAIT_MS 12000U
// The network gets its own, longer cap, because it stopped being the three or
// four seconds a DHCP lease takes. Joining a network whose name is served by
// more than one access point means walking them until one finishes the
// handshake, and on the network this was measured on (2026-09-05) the broken
// one is the louder and is therefore tried first: the address arrives at 10 to
// 12 seconds, and twelve would have been a coin toss decided by how many times
// that access point answered the scan.
#define UI_AUTOPLAY_NETWORK_WAIT_MS 25000U
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
static bool s_brightness_save_pending;
static uint32_t s_brightness_changed_ms;

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

static ui_scroller_t ui_scroller_create(lv_obj_t *parent, int32_t x, int32_t y, int32_t width,
                                        int32_t height)
{
    ui_scroller_t scroller = {0};
    scroller.box = lv_obj_create(parent);
    /* Bare: lv_obj_create brings a border, a background and scrollbars, none
     * of which the label it stands in for had. */
    lv_obj_remove_style_all(scroller.box);
    lv_obj_remove_flag(scroller.box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(scroller.box, x, y);
    lv_obj_set_size(scroller.box, width, height);

    scroller.text = lv_label_create(scroller.box);
    lv_obj_set_pos(scroller.text, 0, 0);
    lv_label_set_long_mode(scroller.text, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(scroller.text, width);
    lv_label_set_text(scroller.text, "");
    return scroller;
}

/* Width of the box's inside, which is what the text has to fit within. Read
 * from the object rather than remembered: callers change the padding per row,
 * and a directory row pads further than a file row. */
static int32_t ui_scroller_view_width(const ui_scroller_t *scroller)
{
    lv_area_t content;
    lv_obj_get_content_coords(scroller->box, &content);
    return lv_area_get_width(&content);
}

static int32_t ui_scroller_text_width(const ui_scroller_t *scroller)
{
    lv_point_t size = {0};
    const char *text = lv_label_get_text(scroller->text);
    if (text == NULL) return 0;
    lv_text_get_size(&size, text, lv_obj_get_style_text_font(scroller->text, LV_PART_MAIN),
                     lv_obj_get_style_text_letter_space(scroller->text, LV_PART_MAIN), 0,
                     LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

/* Whether this line travels at all. A row that is not the one being pointed at
 * keeps its ellipsis: a screen of marquees at once is unreadable. */
static void ui_scroller_set_scrolling(ui_scroller_t *scroller, bool scrolling)
{
    if (scroller->scrolling == scrolling) return;
    scroller->scrolling = scrolling;
    lv_label_set_long_mode(scroller->text,
                           scrolling ? LV_LABEL_LONG_MODE_CLIP : LV_LABEL_LONG_MODE_DOTS);
    /* Content-sized while scrolling, so the label may be wider than the box
     * and have somewhere to travel to; box-sized otherwise, so the ellipsis
     * has an edge to appear at. */
    lv_obj_set_width(scroller->text,
                     scrolling ? LV_SIZE_CONTENT : ui_scroller_view_width(scroller));
    scroller->started_ms = ui_tick_get_ms();
    scroller->applied_x = 0;
    lv_obj_set_x(scroller->text, 0);
}

/* Resting x for the label inside the box. Zero unless the scroller is centred
 * and the line fits: a line too long to fit fills the view, is about to
 * travel, and has nowhere to be centred.
 *
 * Only meaningful while the label is content-sized, which is what scrolling
 * makes it - a box-sized label is already the full width and moving it would
 * push it out of the box. Nothing sets `centred` on a scroller that is ever
 * switched out of scrolling. */
static int32_t ui_scroller_rest_x(const ui_scroller_t *scroller)
{
    if (!scroller->centred || !scroller->scrolling) return 0;
    const int32_t view = ui_scroller_view_width(scroller);
    const int32_t text = ui_scroller_text_width(scroller);
    return text >= view ? 0 : (view - text) / 2;
}

static void ui_scroller_set_text(ui_scroller_t *scroller, const char *text)
{
    if (scroller->text == NULL || text == NULL) return;
    const char *current = lv_label_get_text(scroller->text);
    if (current != NULL && strcmp(current, text) == 0) return;
    lv_label_set_text(scroller->text, text);
    /* A new line starts from its beginning, however far the old one had got. */
    scroller->started_ms = ui_tick_get_ms();
    const int32_t rest = ui_scroller_rest_x(scroller);
    scroller->applied_x = rest;
    lv_obj_set_x(scroller->text, rest);
}

/* Called every frame. Cheap when there is nothing to do: the offset only
 * reaches LVGL when it actually changed, so a line at rest costs one compare. */
static void ui_scroller_tick(ui_scroller_t *scroller, ui_text_scroll_mode_t mode, uint32_t now_ms)
{
    if (scroller->text == NULL || !scroller->scrolling) return;
    if (lv_obj_has_flag(scroller->box, LV_OBJ_FLAG_HIDDEN)) return;
    const int32_t offset = ui_text_scroll_offset(mode, ui_scroller_text_width(scroller),
                                                 ui_scroller_view_width(scroller),
                                                 now_ms - scroller->started_ms);
    /* The two never both apply: the resting offset is zero for any line wide
     * enough to travel, so this is the travel while moving and the centring
     * while still. */
    const int32_t x = offset + ui_scroller_rest_x(scroller);
    if (x == scroller->applied_x) return;
    scroller->applied_x = x;
    lv_obj_set_x(scroller->text, x);
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
    lv_obj_set_size(band, TFT_WIDTH, UI_STRIP_H);
    lv_obj_set_style_bg_color(band, lv_color_hex(UI_COLOR_STRIP), 0);
    lv_obj_set_style_border_width(band, 0, 0);
    lv_obj_set_style_radius(band, 0, 0);
    lv_obj_set_style_pad_all(band, 0, 0);
    lv_obj_clear_flag(band, LV_OBJ_FLAG_SCROLLABLE);

    strip->context = lv_label_create(screen);
    lv_obj_set_pos(strip->context, UI_STRIP_CONTEXT_X, 6);
    lv_obj_set_size(strip->context, UI_STRIP_CONTEXT_W, 19);
    lv_label_set_long_mode(strip->context, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(strip->context, lv_color_hex(UI_COLOR_ACCENT), 0);
    lv_label_set_text(strip->context, context);

    // Centred rather than left-aligned: it is the one thing on the screen read
    // from across the room, and the middle is where the eye goes first.
    strip->clock = lv_label_create(screen);
    lv_obj_set_pos(strip->clock, UI_STRIP_CLOCK_X, 5);
    lv_obj_set_width(strip->clock, UI_STRIP_CLOCK_W);
    lv_obj_set_style_text_align(strip->clock, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(strip->clock, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_label_set_text(strip->clock, "");

    for (int bar = 0; bar < UI_WIFI_BARS; ++bar) {
        lv_obj_t *block = lv_obj_create(screen);
        const int height = 3 + bar * 3;
        lv_obj_set_size(block, 3, height);
        // Grown from a common baseline so the four read as one rising shape.
        lv_obj_set_pos(block, UI_STRIP_BARS_X + bar * 5, 7 + (12 - height));
        lv_obj_set_style_border_width(block, 0, 0);
        lv_obj_set_style_radius(block, 1, 0);
        lv_obj_set_style_pad_all(block, 0, 0);
        lv_obj_set_style_bg_color(block, lv_color_hex(UI_COLOR_BAR_OFF), 0);
        lv_obj_clear_flag(block, LV_OBJ_FLAG_SCROLLABLE);
        strip->bars[bar] = block;
        strip->bar_colour[bar] = UI_COLOR_BAR_OFF;
    }
    strip->rssi = lv_label_create(screen);
    lv_obj_set_pos(strip->rssi, UI_STRIP_RSSI_X, 6);
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

/* Draws the bars, called by LVGL once the strip's own background is down.
 *
 * They are drawn rather than being objects of their own, and the difference is
 * not a matter of taste: LVGL's pool is 64 KB and mostly spoken for, twenty
 * eight more objects ran it dry, and with LV_USE_LOG off an allocation that
 * fails ends in LV_ASSERT_NULL - a bare `while(1)` inside the refresh, which
 * showed up as the UI task pinning its core from the first frame and the
 * screen never appearing at all. One object costs nothing per bar. */
static void ui_buffer_graph_draw(lv_event_t *event)
{
    lv_obj_t *object = lv_event_get_target(event);
    lv_layer_t *layer = lv_event_get_layer(event);
    if (object == NULL || layer == NULL) return;
    lv_area_t box;
    lv_obj_get_coords(object, &box);

    lv_draw_rect_dsc_t bar;
    lv_draw_rect_dsc_init(&bar);
    bar.bg_color = lv_color_hex(UI_COLOR_MUTED);
    bar.bg_opa = LV_OPA_COVER;

    for (size_t column = 0U; column < UI_BUFFER_GRAPH_BARS; ++column) {
        uint8_t percent = 0U;
        if (!ui_buffer_graph_bar(&s_buffer_graph, column, &percent)) continue;
        const int height = ui_buffer_graph_bar_height(percent, UI_SRC_BUFFER_GRAPH_H);
        /* A column with no bar at all - the strip still filling, or a buffer
         * that ran dry - is left as ground rather than drawn one pixel high,
         * which would read as "a little" where there is none. */
        if (height <= 0) continue;
        const int32_t left = box.x1 + (int32_t)column * UI_BUFFER_GRAPH_PITCH;
        // Bars stand on the strip's floor, so they grow upwards.
        const lv_area_t area = {
            .x1 = left,
            .x2 = left + UI_BUFFER_GRAPH_BAR_W - 1,
            .y1 = box.y2 - height + 1,
            .y2 = box.y2,
        };
        lv_draw_rect(layer, &bar, &area);
    }
}

/* Puts the strip on screen or takes it off, and while it is on, walks it.
 *
 * `known` is false where the source has no backlog to report at all: the
 * strip is emptied then rather than fed zeroes, because a buffer nobody is
 * filling is not a buffer running dry. */
static void ui_show_buffer_graph(bool visible, bool known, uint8_t percent)
{
    if (s_source_buffer_graph == NULL) return;
    if (!visible) {
        if (!lv_obj_has_flag(s_source_buffer_graph, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(s_source_buffer_graph, LV_OBJ_FLAG_HIDDEN);
            /* Emptied on the way out, not on the way in: the bars of the
             * station that was playing say nothing about the next one, and
             * clearing here means the strip is already right when it returns.
             */
            ui_buffer_graph_reset(&s_buffer_graph, ui_tick_get_ms());
        }
        return;
    }
    const bool was_hidden = lv_obj_has_flag(s_source_buffer_graph, LV_OBJ_FLAG_HIDDEN);
    if (was_hidden) lv_obj_clear_flag(s_source_buffer_graph, LV_OBJ_FLAG_HIDDEN);
    if (!known) {
        ui_buffer_graph_reset(&s_buffer_graph, ui_tick_get_ms());
        return;
    }
    /* Invalidated only when a bar was actually taken, or when the strip has
     * just come back: the draw runs from the refresh, and asking for one every
     * poll would redraw the footer a hundred times a second. */
    if (ui_buffer_graph_step(&s_buffer_graph, ui_tick_get_ms(), percent) || was_hidden) {
        lv_obj_invalidate(s_source_buffer_graph);
    }
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
    /* Set only where the buffer is what the footer's left slot is showing. The
     * strip is the same reading in another form, so it belongs to that one
     * branch and not to the time a file or a track shows there. */
    bool buffer_slot = false;
    uint8_t buffer_fill = 0U;
    bool buffer_known = false;
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
        buffer_slot = true;
        buffer_known = player_control_input_fill(&buffer_fill);
        if (buffer_known) {
            snprintf(left_text, sizeof(left_text), "Буфер %u%%", (unsigned int)buffer_fill);
        } else {
            // Nothing playing, or a source with no backlog at all. A dash says
            // that; a zero would claim the buffer had run dry.
            snprintf(left_text, sizeof(left_text), "Буфер --");
        }
    }
    /* One reading, two faces, and exactly one of them on screen: the strip
     * only when the setting asks for it and the slot is the buffer's, so a
     * file's elapsed time is never drawn over by bars about a buffer it does
     * not have. */
    const bool graph_slot = buffer_slot &&
                            s_device_settings.buffer_view == DEVICE_BUFFER_VIEW_GRAPH;
    ui_show_buffer_graph(graph_slot, buffer_known, buffer_fill);
    ui_set_label_text_if_changed(s_source_buffer, graph_slot ? "" : left_text);

    if (have_bar) {
        lv_obj_clear_flag(s_source_progress, LV_OBJ_FLAG_HIDDEN);
        lv_obj_t *played = lv_obj_get_child(s_source_progress, 0);
        if (played != NULL) {
            const int32_t width = (int32_t)(((unsigned int)UI_CONTENT_W * played_percent) / 100U);
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
        const int32_t width = (int32_t)(((unsigned int)UI_SRC_VOLUME_BAR_W * volume) / 100U);
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

    /* A bitmap per state rather than one recoloured: an outline heart, a solid
     * one and a struck-through one are different shapes, and marks told apart
     * by brightness alone would read as "liked, dimly" rather than as three
     * different answers. */
    const int8_t like = !snapshot->track_likeable ? -1
                        : snapshot->track_disliked ? 2
                        : snapshot->track_liked    ? 1
                                                   : 0;
    if (like != s_source_like_shown) {
        s_source_like_shown = like;
        if (like < 0) {
            lv_obj_add_flag(s_source_like, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_image_set_src(s_source_like, like == 2 ? &ui_feed_icon_heart_slash_16
                                            : like == 1 ? &ui_feed_icon_heart_filled_16
                                                        : &ui_feed_icon_heart_16);
            /* The rejection is muted like the empty heart, not accented like
             * the filled one: it is not something the screen is celebrating,
             * and the shape already says which of the two it is. */
            lv_obj_set_style_image_recolor(
                s_source_like, lv_color_hex(like == 1 ? UI_COLOR_ACCENT : UI_COLOR_MUTED), 0);
            lv_obj_clear_flag(s_source_like, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* Puts `state` on the performer row, or gives the row back to the performer.
 *
 * Hiding one of the two is what makes this safe. They share a row because
 * there is no third line to spare, and an earlier version relied on the
 * performer being empty whenever a state was worth showing - which is false on
 * pause, where the title is still there and the two drew on top of each
 * other. */
static void ui_set_state_line(const char *state, const char *artist, bool error)
{
    const bool show_state = state != NULL && state[0] != '\0';
    ui_set_label_text_if_changed(s_source_status, show_state ? state : "");
    /* Set on every pass rather than only when it changes: the label is shared
     * by every state the screen shows, and a colour left behind would paint the
     * next "Connecting..." in the failure's red. */
    lv_obj_set_style_text_color(s_source_status,
                                lv_color_hex(error ? UI_COLOR_ERROR : UI_COLOR_DIM), 0);
    ui_set_label_text_if_changed(s_source_artist, show_state ? "" : artist);
    if (show_state) {
        lv_obj_add_flag(s_source_artist, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_source_status, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_source_status, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_source_artist, LV_OBJ_FLAG_HIDDEN);
    }
}

/* The same three readings either way; only their shape follows the space they
 * are given - one line across the wide panel, a stacked column beside the
 * cover on the narrow one. */
static void ui_set_stream_readings(const player_snapshot_t *snapshot)
{
    char text[64];
#if UI_SRC_STREAM_LINES
    ui_radio_stream_lines(text, sizeof(text), snapshot->codec, snapshot->bitrate_kbps,
                          snapshot->sample_rate_hz);
#else
    ui_radio_stream_text(text, sizeof(text), snapshot->codec, snapshot->bitrate_kbps,
                         snapshot->sample_rate_hz);
#endif
    ui_set_label_text_if_changed(s_source_stream, text);
}

static void ui_update_files_status(const player_snapshot_t *snapshot)
{
    audio_tags_t tags;
    const bool tagged = player_control_track_tags(&tags);
    ui_now_playing_t now;
    ui_now_playing_for_file(snapshot->context, snapshot->stream_title,
                            tagged ? &tags : NULL, &now);

    ui_set_label_text_if_changed(s_source_title, now.heading);
    ui_scroller_set_text(&s_source_detail, now.title);
    // The performer row is the state line's whenever there is a state worth
    // naming. Pause is not one - the badge says it.
    /* A file that will not open is a failure too, and it reaches this line the
     * same way the radio's does. */
    ui_set_state_line(snapshot->playback_state == PLAYER_PLAYBACK_STOPPED ? "Выберите файл" : "",
                      now.artist, snapshot->playback_state == PLAYER_PLAYBACK_ERROR);
    ui_set_stream_readings(snapshot);
}

static void ui_update_radio_status(const player_snapshot_t *snapshot)
{
    if (snapshot == NULL) return;
    ui_update_playback_marks(snapshot);
    if (audio_source_is_files(ui_player_state_source(&s_player_ui))) {
        ui_update_files_status(snapshot);
        return;
    }
    /* Both station sources render the same way: a name on top, a track under
     * it, and one line of state. Only where the name comes from differs - the
     * catalog file for the radio, the account's list for Yandex. */
    const audio_source_t source = ui_player_state_source(&s_player_ui);
    if (!audio_source_is_stations(source)) return;

    // While a station switch is pending confirmation, snapshot->active_item_index
    // still reflects the previous station; keep showing the one the user just
    // picked instead of flipping back to the old one for the pending window.
    size_t pending_item_index;
    const bool pending =
        ui_player_state_pending_item(&s_player_ui, &pending_item_index);
    const size_t display_item_index =
        pending ? pending_item_index : snapshot->active_item_index;

    /* The stream's own name is only about the station the snapshot describes,
     * so while a switch is pending it names the previous one. The list is the
     * only source that can answer for the station just picked. */
    const char *stream_name = pending ? "" : snapshot->context;
    const char *list_name = NULL;
    bool name_from_list = true;
    if (source == AUDIO_SOURCE_YANDEX) {
        yandex_station_t station;
        if (yandex_catalog_station_at(display_item_index, &station)) {
            list_name = station.name;
        } else if (!pending) {
            /* The list was refreshed under the playing station and the row is
             * gone. What the chain reports is still its name. */
            list_name = snapshot->context;
        }
    } else {
        const station_catalog_entry_t *entry =
            player_control_station_at(display_item_index);
        if (entry != NULL) {
            list_name = entry->name;
            name_from_list = entry->flag != 0;
        }
    }

    ui_now_playing_t now;
    ui_now_playing_for_station(name_from_list, list_name != NULL ? list_name : "",
                               stream_name, snapshot->stream_title, &now);
    /* A station the list can no longer answer for keeps the name the previous
     * pass put there rather than having it blanked; what is playing is still
     * the stream's to tell, so the rows below it go on either way. */
    if (list_name != NULL) {
        ui_set_label_text_if_changed(s_source_title, now.heading);
    }
    ui_scroller_set_text(&s_source_detail, now.title);

    // Playing and paused both leave the row to the performer: one needs no
    // announcement, the other has the badge. Connecting, reconnecting and
    // failure are the states worth a line.
    const bool settled = snapshot->playback_state == PLAYER_PLAYBACK_PLAYING ||
                         snapshot->playback_state == PLAYER_PLAYBACK_PAUSED;
    ui_set_state_line(settled ? "" : ui_radio_state_text(snapshot->playback_state),
                      now.artist, snapshot->playback_state == PLAYER_PLAYBACK_ERROR);

    ui_set_stream_readings(snapshot);
}

static uint32_t ui_tick_get_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static const char *ui_feed_item_title(ui_feed_item_t item)
{
    return ui_menu_item_label((ui_menu_item_t)item);
}

/* Both home screens carry the same items, so the switch in Settings has to
 * reach both. Called wherever settings.csv is read or written, rather than
 * asked for at draw time: the models also move the cursor off a row that has
 * just gone away, which is a change, not a query. */
static void ui_apply_yandex_visibility(void)
{
    ui_menu_set_yandex_visible(&s_menu, s_device_settings.yandex_music);
    ui_feed_model_set_yandex_visible(&s_feed_model, s_device_settings.yandex_music);
}

/* Whether this device has a home screen at all. Asked each time rather than
 * settled once at boot: the Yandex switch in Settings can take away the last
 * source beyond the radio, which turns a device that had a home screen into
 * one that does not, without a reboot in between. */
static bool ui_home_screen_exists(void)
{
    return ui_menu_home_screen_needed(
        ui_menu_visible_count(ui_menu_yandex_visible(&s_menu)));
}

static void ui_update_feed_screen(void)
{
    const ui_feed_item_t selected = ui_feed_model_selected(&s_feed_model);
    /* One entry per slot in view, ordered left to right, with the selected
     * item in the middle. Portrait drops the outer pair rather than moving it
     * inwards: at 240 px there is no room for a fourth and fifth tile that is
     * not either off the panel or on top of its neighbour. */
#if UI_FEED_SLOTS == 3
    const int offsets[UI_FEED_SLOTS] = {-1, 0, 1};
    const int centers[UI_FEED_SLOTS] = {
        UI_FEED_CENTER_X - UI_FEED_INNER_DX, UI_FEED_CENTER_X,
        UI_FEED_CENTER_X + UI_FEED_INNER_DX,
    };
    const ui_feed_icon_size_t sizes[UI_FEED_SLOTS] = {
        UI_FEED_ICON_MEDIUM, UI_FEED_ICON_LARGE, UI_FEED_ICON_MEDIUM,
    };
    const int pixels[UI_FEED_SLOTS] = {
        UI_FEED_ICON_MEDIUM_PX, UI_FEED_ICON_LARGE_PX, UI_FEED_ICON_MEDIUM_PX,
    };
    const uint32_t colors[UI_FEED_SLOTS] = {
        UI_COLOR_FEED_NEAR, UI_COLOR_ACCENT, UI_COLOR_FEED_NEAR,
    };
#else
    const int offsets[UI_FEED_SLOTS] = {-2, -1, 0, 1, 2};
    const int centers[UI_FEED_SLOTS] = {
        UI_FEED_CENTER_X - UI_FEED_OUTER_DX, UI_FEED_CENTER_X - UI_FEED_INNER_DX,
        UI_FEED_CENTER_X,
        UI_FEED_CENTER_X + UI_FEED_INNER_DX, UI_FEED_CENTER_X + UI_FEED_OUTER_DX,
    };
    const ui_feed_icon_size_t sizes[UI_FEED_SLOTS] = {
        UI_FEED_ICON_SMALL, UI_FEED_ICON_MEDIUM, UI_FEED_ICON_LARGE,
        UI_FEED_ICON_MEDIUM, UI_FEED_ICON_SMALL,
    };
    const int pixels[UI_FEED_SLOTS] = {
        UI_FEED_ICON_SMALL_PX, UI_FEED_ICON_MEDIUM_PX, UI_FEED_ICON_LARGE_PX,
        UI_FEED_ICON_MEDIUM_PX, UI_FEED_ICON_SMALL_PX,
    };
    const uint32_t colors[UI_FEED_SLOTS] = {
        UI_COLOR_FEED_FAR, UI_COLOR_FEED_NEAR, UI_COLOR_ACCENT,
        UI_COLOR_FEED_NEAR, UI_COLOR_FEED_FAR,
    };
#endif
    const bool yandex = ui_feed_model_yandex_visible(&s_feed_model);
    /* The carousel wraps over what is on screen, not over the enum: with an
     * item switched off, stepping past the last one has to land on the first
     * visible one, and the dot row has to lose a dot. */
    const int visible = (int)ui_menu_visible_count(yandex);
    const int position = (int)ui_menu_visible_position((ui_menu_item_t)selected, yandex);
    for (size_t slot = 0; slot < (size_t)UI_FEED_SLOTS; ++slot) {
        int index = position + offsets[slot];
        while (index < 0) index += visible;
        index %= visible;
        const ui_feed_item_t item = (ui_feed_item_t)ui_menu_visible_item_at((uint8_t)index, yandex);
        const bool center = slot == (size_t)UI_FEED_SLOTS / 2U;
        lv_image_set_src(s_feed_icons[slot], ui_feed_icon_image(item, sizes[slot]));
        /* The tile is bigger than the icon inside it, so the object is placed
         * by its own box and the image centred within it. Everything else is
         * exactly icon-sized. */
        const int box = center ? UI_FEED_TILE : pixels[slot];
        lv_obj_set_size(s_feed_icons[slot], box, box);
        lv_obj_set_pos(s_feed_icons[slot], centers[slot] - box / 2, UI_FEED_AXIS_Y - box / 2);
        /* An A8 bitmap has no colour of its own: LVGL blends it with the
         * recolour, which is what lets one image serve every slot. */
        const bool enabled =
            ui_menu_item_is_enabled((ui_menu_item_t)item, yandex, s_last_wifi_connected);
        lv_obj_set_style_image_recolor(
            s_feed_icons[slot],
            lv_color_hex(enabled ? colors[slot] : UI_COLOR_DISABLED), 0);
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
    /* Re-centred here rather than at creation: the row is one dot narrower
     * when an item is switched off, and an off-centre row reads as a bug. */
    const int dots_left = UI_FEED_CENTER_X - (visible * UI_FEED_DOT_PITCH - UI_FEED_DOT) / 2;
    for (int index = 0; index < UI_FEED_ITEM_COUNT; ++index) {
        if (index >= visible) {
            lv_obj_add_flag(s_feed_dots[index], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(s_feed_dots[index], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(s_feed_dots[index], dots_left + index * UI_FEED_DOT_PITCH);
        lv_obj_set_style_bg_color(s_feed_dots[index],
                                  lv_color_hex(index == position ? UI_COLOR_ACCENT
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
    lv_obj_set_style_text_font(s_feed_screen, UI_FONT_BODY, 0);
    ui_status_strip_create(s_feed_screen, &s_feed_strip, "jRadio");

    s_feed_title = lv_label_create(s_feed_screen);
    lv_obj_set_pos(s_feed_title, 0, UI_STRIP_H + 8);
    lv_obj_set_size(s_feed_title, TFT_WIDTH, 28);
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
    lv_obj_set_style_text_font(s_feed_title, UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(s_feed_title, lv_color_hex(UI_COLOR_TEXT), 0);
    s_feed_notice = lv_label_create(s_feed_screen);
    lv_obj_set_pos(s_feed_notice, 8, UI_FEED_NOTICE_Y);
    lv_obj_set_size(s_feed_notice, TFT_WIDTH - 16, 18);
    lv_obj_set_style_text_align(s_feed_notice, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_feed_notice, lv_color_hex(0xFFCC80), 0);
    lv_label_set_text(s_feed_notice, "");
    for (size_t index = 0; index < (size_t)UI_FEED_SLOTS; ++index) {
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

/* Rows, not items: a hidden item leaves no gap, so the row a name lands on
 * depends on what is switched off above it. The model does that arithmetic. */
static void ui_update_menu_highlight(void)
{
    const uint8_t selected = ui_menu_selected_index(&s_menu);
    const bool yandex = ui_menu_yandex_visible(&s_menu);
    const uint8_t visible = ui_menu_visible_count(yandex);
    for (uint8_t row = 0; row < UI_MENU_ITEM_COUNT; ++row) {
        if (row >= visible) {
            // Hidden rather than blanked: the row tile is opaque and would
            // otherwise leave a bar of background colour below the last name.
            lv_obj_add_flag(s_menu_rows[row], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_menu_icons[row], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(s_menu_rows[row], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_menu_icons[row], LV_OBJ_FLAG_HIDDEN);
        const ui_menu_item_t item = ui_menu_visible_item_at(row, yandex);
        const bool is_selected = (uint8_t)item == selected;
        const bool enabled = ui_menu_item_is_enabled(item, yandex, s_last_wifi_connected);
        // Raised tile plus accent text, the way the player screen marks what
        // it is playing. The arrow the old highlight needed is gone: a filled
        // row says the same thing without spending a character on it.
        lv_obj_set_style_bg_color(s_menu_rows[row],
                                  lv_color_hex(is_selected ? UI_COLOR_SELECTED
                                                           : UI_COLOR_GROUND), 0);
        lv_obj_set_style_bg_opa(s_menu_rows[row], LV_OPA_COVER, 0);
        /* The cursor still lands on it - the row is real, it just cannot be
           started right now, and the press says why. */
        lv_obj_set_style_text_color(s_menu_rows[row],
                                    lv_color_hex(!enabled       ? UI_COLOR_DISABLED
                                                 : is_selected  ? UI_COLOR_ACCENT
                                                                : UI_COLOR_MUTED), 0);
        lv_label_set_text(s_menu_rows[row], ui_menu_item_label(item));
        lv_image_set_src(s_menu_icons[row],
                         ui_feed_icon_image((ui_feed_item_t)item, UI_FEED_ICON_SMALL));
        // The icon follows the text rather than staying lit: a row that is not
        // under the cursor should read as one thing, not as a bright mark with
        // dim writing next to it.
        lv_obj_set_style_image_recolor(s_menu_icons[row],
                                       lv_color_hex(!enabled      ? UI_COLOR_DISABLED
                                                    : is_selected ? UI_COLOR_ACCENT
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
    lv_obj_set_style_text_font(s_menu_screen, UI_FONT_TITLE, 0);

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
    lv_obj_set_style_text_font(s_menu_notice, UI_FONT_BODY, 0);
    lv_obj_set_pos(s_menu_notice, 12, TFT_HEIGHT - 19);
    lv_obj_set_style_text_color(s_menu_notice, lv_color_hex(UI_COLOR_NOTICE), 0);
    ui_update_menu_highlight();
}

/* Fills `text` with what the row at `list_index` should read, reports whether
 * that row is the one currently playing, and gives the mark that goes in front
 * of it - the folder glyph, the playlist glyph, or none at all. */
/* Longest row either list can produce: a full-length USB name. Station rows
 * are far shorter - a three-digit ordinal, a space and a 96-byte name. */
#define UI_LIST_ROW_TEXT_MAX (FILE_BROWSER_NAME_MAX_LEN + 8U)

static bool ui_list_row_text(size_t list_index, char *text, size_t text_size,
                             bool *active, const char **mark)
{
    *active = false;
    *mark = NULL;
    if (!ui_list_shows_files()) {
        const station_catalog_entry_t *entry = player_control_station_at(list_index);
        /* The name alone. The index used to be part of this string and
         * travelled with it when the row scrolled, which put the number
         * halfway across the screen and then off it - the one part of the row
         * that should never move. It is a label of its own now, drawn by the
         * caller beside the box. */
        snprintf(text, text_size, "%s", entry == NULL ? "" : entry->name);
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
    /* Directories are marked rather than merely sorted first, so the row tells
     * you what the encoder click will do before you press it. The mark itself
     * is drawn by the caller, from its own label - see s_station_list_icons.
     *
     * A playlist gets a mark of its own rather than the folder's. Both open
     * something, so both are marked, but a playlist is not a place on the
     * drive: it names tracks from anywhere on it, and the folder glyph would
     * promise a folder that is not there. */
    if (entry.kind == FILE_BROWSER_ENTRY_DIRECTORY) {
        *mark = LV_SYMBOL_DIRECTORY;
    } else if (entry.kind == FILE_BROWSER_ENTRY_PLAYLIST) {
        *mark = LV_SYMBOL_LIST;
    }
    // Inside a playlist the name is the path the file wrote; the row has room
    // for the track, not for the folders above it.
    snprintf(text, text_size, "%s", file_browser_display_name(entry.name));
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
            lv_obj_add_flag(s_station_list_rows[row].box, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_station_list_icons[row], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_station_list_numbers[row], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(s_station_list_rows[row].box, LV_OBJ_FLAG_HIDDEN);
        char text[UI_LIST_ROW_TEXT_MAX];
        bool active = false;
        const char *mark = NULL;
        (void)ui_list_row_text((size_t)entry_index, text, sizeof(text), &active, &mark);
        // The name starts after the mark on a row that has one and at the edge
        // everywhere else, so the two never overlap and a file's name is not
        // indented for a mark it does not have.
        const bool marked = mark != NULL;
        if (marked) {
            ui_set_label_text_if_changed(s_station_list_icons[row], mark);
            lv_obj_clear_flag(s_station_list_icons[row], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_station_list_icons[row], LV_OBJ_FLAG_HIDDEN);
        }
        /* Stations are numbered, files are not: a browser row can be ".." or a
         * directory, and neither is an nth of anything. A station row is never
         * marked, so the number and the folder mark never share a row and the
         * two indents never add up. */
        const bool numbered = !ui_list_shows_files();
        if (numbered) {
            /* Two digits for any catalogue this list can scroll, and room for
             * what an unsigned can actually print - the compiler checks the
             * latter and does not know about the former. */
            char number[12];
            snprintf(number, sizeof(number), "%02u", (unsigned)(entry_index + 1));
            ui_set_label_text_if_changed(s_station_list_numbers[row], number);
            lv_obj_clear_flag(s_station_list_numbers[row], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_station_list_numbers[row], LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_set_style_pad_left(s_station_list_rows[row].box,
                                  numbered ? 8 + UI_LIST_NUMBER_W
                                  : marked ? 8 + UI_LIST_ICON_W
                                           : 8,
                                  0);
        const bool selected = row == cursor_row;
        lv_obj_set_style_bg_color(s_station_list_rows[row].box,
                                  lv_color_hex(selected ? UI_COLOR_SELECTED
                                                        : UI_COLOR_GROUND), 0);
        lv_obj_set_style_bg_opa(s_station_list_rows[row].box, LV_OPA_COVER, 0);
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
        lv_obj_set_style_text_color(s_station_list_rows[row].box, lv_color_hex(row_colour), 0);
        // The index takes the row's colour: it is part of the row, not a
        // fixture beside it, and a number that stayed muted under the cursor
        // would read as a different row from the name next to it.
        lv_obj_set_style_text_color(s_station_list_numbers[row], lv_color_hex(row_colour), 0);
        // And the patch it sits on takes the row's background, or the column
        // would stay dark under a lit row.
        lv_obj_set_style_bg_color(s_station_list_numbers[row],
                                  lv_color_hex(selected ? UI_COLOR_SELECTED : UI_COLOR_GROUND), 0);
        // Only the row under the cursor scrolls: a screen of six marquees at
        // once is unreadable, and the row being pointed at is the one whose
        // full name the user is actually after. The others keep the ellipsis.
        //
        // Set before the text: both restart the animation, and doing it in
        // this order means a row that was already scrolling the same text
        // carries on rather than jumping back to the start.
        ui_scroller_set_scrolling(&s_station_list_rows[row], selected);
        ui_scroller_set_text(&s_station_list_rows[row], text);
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

/* Whether there is a player screen worth returning to. The idle timers on the
 * list screens exist to put it back; with nothing playing there is nothing
 * behind them, so browsing is allowed to take as long as the user wants. */
static bool ui_playback_running(const player_snapshot_t *snapshot)
{
    return snapshot->playback_state == PLAYER_PLAYBACK_PLAYING ||
           snapshot->playback_state == PLAYER_PLAYBACK_PAUSED ||
           snapshot->playback_state == PLAYER_PLAYBACK_CONNECTING ||
           snapshot->playback_state == PLAYER_PLAYBACK_RECONNECTING;
}

static void ui_load_menu_screen(void);
/* Defined with the rest of the player plumbing, further down; the Yandex
 * screen needs them before that - to post its commands, to open the list the
 * active source owns, and to put back the screen it was opened over. */
static bool ui_submit_player_command(const player_command_t *command);
static void ui_show_station_list(void);
static void ui_render_player_state(void);

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
    lv_obj_set_style_text_font(s_settings_screen, UI_FONT_BODY, 0);

    lv_obj_t *title = lv_label_create(s_settings_screen);
    lv_label_set_text(title, "Настройки");
    lv_obj_set_pos(title, 12, 8);
    // Matches the group headings under it: a heading smaller than the rows it
    // introduces reads as a mistake.
    lv_obj_set_style_text_font(title, UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_TEXT), 0);

    for (size_t row = 0; row < UI_SETTINGS_MAX_ROWS; ++row) {
        s_settings_rows[row] = lv_label_create(s_settings_screen);
        lv_obj_set_pos(s_settings_rows[row], 10, UI_SET_ROW_Y + (int)row * UI_SET_ROW_PITCH);
        lv_obj_set_width(s_settings_rows[row], UI_CONTENT_W);
        lv_obj_set_height(s_settings_rows[row], UI_SET_ROW_H);
        lv_obj_set_style_pad_left(s_settings_rows[row], 6, 0);
        lv_obj_set_style_pad_top(s_settings_rows[row], 2, 0);
        lv_label_set_long_mode(s_settings_rows[row], LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(s_settings_rows[row], lv_color_hex(UI_COLOR_TEXT), 0);
        lv_obj_set_style_bg_opa(s_settings_rows[row], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(s_settings_rows[row], lv_color_hex(UI_COLOR_GROUND), 0);
        lv_label_set_text(s_settings_rows[row], "");
    }
    /* In the right margin, clear of the rows: the text column ends at 300 and
     * a row with a switch starts at 58, so nothing here overlaps either. */
    s_settings_more_above = lv_label_create(s_settings_screen);
    lv_obj_set_pos(s_settings_more_above, UI_SET_CHEVRON_X, UI_SET_ROW_Y - 15);
    lv_obj_set_style_text_color(s_settings_more_above, lv_color_hex(UI_COLOR_DIM), 0);
    lv_label_set_text(s_settings_more_above, "");

    s_settings_more_below = lv_label_create(s_settings_screen);
    lv_obj_set_pos(s_settings_more_below, UI_SET_CHEVRON_X,
                   UI_SET_ROW_Y + UI_SETTINGS_MAX_ROWS * UI_SET_ROW_PITCH);
    lv_obj_set_style_text_color(s_settings_more_below, lv_color_hex(UI_COLOR_DIM), 0);
    lv_label_set_text(s_settings_more_below, "");

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
    // Stops short of the chevron column, which shares this line.
    lv_obj_set_width(s_settings_notice, TFT_WIDTH - 34);
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
    lv_obj_set_size(s_settings_web_band, TFT_WIDTH, UI_SET_BAND_H);
    lv_obj_set_style_bg_color(s_settings_web_band, lv_color_hex(UI_COLOR_STRIP), 0);
    lv_obj_set_style_bg_opa(s_settings_web_band, LV_OPA_COVER, 0);

    /* The "web" tag that used to sit here is gone: the address starts with
     * http:// and says what it is, and the room it took is what the right-hand
     * half of the band needs to say the click leads somewhere. */
    s_settings_web_address = lv_label_create(s_settings_web_band);
    lv_obj_set_pos(s_settings_web_address, UI_SET_BAND_PAD, (UI_SET_BAND_H - 19) / 2);
    lv_obj_set_width(s_settings_web_address, UI_SET_BAND_ADDRESS_W);
    lv_label_set_long_mode(s_settings_web_address, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_settings_web_address, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_label_set_text(s_settings_web_address, "");

    s_settings_web_hint = lv_label_create(s_settings_web_band);
    lv_obj_align(s_settings_web_hint, LV_ALIGN_RIGHT_MID, -UI_SET_BAND_PAD, 0);
    lv_obj_set_style_text_color(s_settings_web_hint, lv_color_hex(UI_COLOR_DIM), 0);
    lv_label_set_text(s_settings_web_hint, "");

    /* A cover over this screen rather than a screen of its own: the code is a
     * detail of the band, and the settings loop already runs - a second screen
     * would mean a second copy of everything that keeps it up to date. */
    s_qr_overlay = lv_obj_create(s_settings_screen);
    lv_obj_remove_style_all(s_qr_overlay);
    lv_obj_set_pos(s_qr_overlay, 0, 0);
    lv_obj_set_size(s_qr_overlay, TFT_WIDTH, TFT_HEIGHT);
    lv_obj_set_style_bg_color(s_qr_overlay, lv_color_hex(UI_COLOR_GROUND), 0);
    lv_obj_set_style_bg_opa(s_qr_overlay, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_qr_overlay, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *card = lv_obj_create(s_qr_overlay);
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, (TFT_WIDTH - UI_QR_CARD) / 2, UI_QR_CARD_Y);
    lv_obj_set_size(card, UI_QR_CARD, UI_QR_CARD);
    lv_obj_set_style_bg_color(card, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 4, 0);

    s_qr_code = lv_qrcode_create(card);
    lv_qrcode_set_size(s_qr_code, UI_QR_SIZE);
    lv_qrcode_set_dark_color(s_qr_code, lv_color_black());
    lv_qrcode_set_light_color(s_qr_code, lv_color_white());
    lv_obj_set_pos(s_qr_code, (UI_QR_CARD - UI_QR_SIZE) / 2, (UI_QR_CARD - UI_QR_SIZE) / 2);

    s_qr_caption = lv_label_create(s_qr_overlay);
    lv_obj_set_pos(s_qr_caption, 10, UI_QR_CARD_Y + UI_QR_CARD + 6);
    lv_obj_set_width(s_qr_caption, UI_CONTENT_W);
    lv_obj_set_style_text_align(s_qr_caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_qr_caption, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_qr_caption, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_label_set_text(s_qr_caption, "");

    s_qr_back = lv_label_create(s_qr_overlay);
    lv_obj_set_pos(s_qr_back, 10, UI_QR_CARD_Y + UI_QR_CARD + 28);
    lv_obj_set_width(s_qr_back, UI_CONTENT_W);
    lv_obj_set_style_text_align(s_qr_back, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_qr_back, lv_color_hex(UI_COLOR_DIM), 0);
    lv_label_set_text(s_qr_back, "");
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
    case UI_SETTINGS_ROW_SCROLL_FIELD:
        snprintf(text, text_size, "  %s: %s", english ? "Scrolling" : "Скроллинг",
                 s_device_settings.scroll == DEVICE_SCROLL_LEFT
                     ? (english ? "Left" : "Влево")
                     : (english ? "Left-right" : "Влево-вправо"));
        break;
    case UI_SETTINGS_ROW_BUFFER_FIELD:
        snprintf(text, text_size, "  %s: %s", english ? "Buffer" : "Буфер",
                 s_device_settings.buffer_view == DEVICE_BUFFER_VIEW_GRAPH
                     ? (english ? "Graph" : "График")
                     : (english ? "Text" : "Текст"));
        break;
    case UI_SETTINGS_ROW_AUTOPLAY_FIELD:
        snprintf(text, text_size, "  %s: %s", english ? "Autoplay" : "Автовоспроизведение",
                 s_device_settings.autoplay ? "ON" : "OFF");
        break;
    case UI_SETTINGS_ROW_YANDEX_FIELD:
        snprintf(text, text_size, "  %s: %s", english ? "Yandex Music" : "Яндекс Музыка",
                 s_device_settings.yandex_music ? "ON" : "OFF");
        break;
    case UI_SETTINGS_ROW_BRIGHTNESS_FIELD:
        /* Angle brackets while the knob owns the value: the cursor already
         * says which row, and this is the only thing that says the next click
         * of the encoder changes a number instead of moving on. */
        snprintf(text, text_size,
                 ui_settings_model_is_editing(&s_settings_model) ? "  %s: <%d>" : "  %s: %d",
                 english ? "Brightness" : "Яркость", (int)s_device_settings.brightness);
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
    case UI_SETTINGS_ROW_YANDEX_FIELD:
        *index = 3U;
        *value = s_device_settings.yandex_music;
        return true;
    default:
        return false;
    }
}

static void ui_hide_qr(void)
{
    s_qr_open = false;
    s_qr_shown[0] = '\0';
    lv_obj_add_flag(s_qr_overlay, LV_OBJ_FLAG_HIDDEN);
}

/* Brings the code on screen in line with the address the band is showing.
 *
 * The address can change while the code is up - saving a network from the web
 * moves the box off its own access point - and a QR still offering the old one
 * is worse than none: the phone reports a failure that looks like its own. */
static void ui_apply_qr(const char *payload, bool available, const char *caption,
                        bool english)
{
    if (!s_qr_open) return;
    if (!available) {
        ui_hide_qr();
        return;
    }
    ui_set_label_text_if_changed(s_qr_caption, caption);
    ui_set_label_text_if_changed(s_qr_back,
                                 english ? "press to return" : "Нажмите, чтобы вернуться");
    if (strcmp(payload, s_qr_shown) == 0) return;
    if (lv_qrcode_update(s_qr_code, payload, (uint32_t)strlen(payload)) != LV_RESULT_OK) {
        /* Nothing to show and nothing to say about it: a blank white card
         * would read as a code the phone is failing to scan. */
        ui_hide_qr();
        return;
    }
    snprintf(s_qr_shown, sizeof(s_qr_shown), "%s", payload);
}

static void ui_update_settings_web_band(void)
{
    const wifi_provisioning_status_t status = wifi_provisioning_status();
    const bool english = s_device_settings.language == DEVICE_LANGUAGE_EN;
    char text[64];
    ui_web_address_text(status.mode, status.ipv4, status.active_ssid, english,
                        UI_SET_BAND_SHOW_SCHEME, text, sizeof(text));
    ui_set_label_text_if_changed(s_settings_web_address, text);

    char payload[sizeof(s_qr_shown)];
    const bool available = ui_web_address_qr(status.mode, status.ipv4, status.active_ssid,
                                             payload, sizeof(payload));
    /* The offer only appears when there is something behind it. While the box
     * is still joining a network there is no address to encode, and a line
     * that says "press for QR" and then does nothing is a fault report. */
    ui_set_label_text_if_changed(s_settings_web_hint, available ? "press for QR" : "");

    /* The band is the last cursor stop, so it highlights like a row: the same
     * tile colour and the same accent, or the screen has a selection nobody
     * can see. */
    const bool selected =
        ui_settings_model_selected(&s_settings_model) == UI_SETTINGS_ROW_ADDRESS_BAND;
    lv_obj_set_style_bg_color(s_settings_web_band,
                              lv_color_hex(selected ? UI_COLOR_CURSOR : UI_COLOR_STRIP), 0);
    lv_obj_set_style_text_color(s_settings_web_address,
                                lv_color_hex(selected ? UI_COLOR_ACCENT : UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_color(s_settings_web_hint,
                                lv_color_hex(selected ? UI_COLOR_ACCENT : UI_COLOR_DIM), 0);
    ui_apply_qr(payload, available, text, english);
}

static void ui_show_qr(void)
{
    if (s_qr_open) return;
    s_qr_open = true;
    s_qr_opened_ms = ui_tick_get_ms();
    /* Empty, so the next band update builds the code rather than deciding it
     * is already the one on screen. */
    s_qr_shown[0] = '\0';
    lv_obj_clear_flag(s_qr_overlay, LV_OBJ_FLAG_HIDDEN);
    /* Which also takes the overlay straight back down if the address went away
     * between the band being drawn and the button being pressed. */
    ui_update_settings_web_band();
}

static void ui_update_settings(void)
{
    if (!s_settings_open) return;
    ui_update_settings_web_band();
    const size_t row_count = ui_settings_model_row_count(&s_settings_model);
    const ui_settings_row_id_t selected = ui_settings_model_selected(&s_settings_model);
    const size_t window_top = ui_settings_model_window_top(&s_settings_model,
                                                           UI_SETTINGS_MAX_ROWS);
    for (size_t index = 0; index < UI_SETTINGS_SWITCH_COUNT; ++index) {
        lv_obj_add_flag(s_settings_switches[index], LV_OBJ_FLAG_HIDDEN);
    }
    /* Arrows rather than a scroll bar: the rows already reach both edges, and
     * the one thing the user needs to know is that the list continues. */
    ui_set_label_text_if_changed(s_settings_more_above,
                                 ui_settings_model_has_rows_above(&s_settings_model) ? LV_SYMBOL_UP
                                                                                     : "");
    ui_set_label_text_if_changed(s_settings_more_below,
                                 ui_settings_model_has_rows_below(&s_settings_model,
                                                                  UI_SETTINGS_MAX_ROWS)
                                     ? LV_SYMBOL_DOWN
                                     : "");
    for (size_t row = 0; row < UI_SETTINGS_MAX_ROWS; ++row) {
        const size_t model_row = window_top + row;
        if (model_row >= row_count) {
            /* Hidden, not merely blanked: a row's background is opaque, and
             * with the taller pitch an unused row reaches into the web band. */
            lv_label_set_text(s_settings_rows[row], "");
            lv_obj_add_flag(s_settings_rows[row], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(s_settings_rows[row], LV_OBJ_FLAG_HIDDEN);
        const ui_settings_row_t item = ui_settings_model_row_at(&s_settings_model, model_row);
        char text[96];
        ui_settings_row_text(&item, text, sizeof(text));
        ui_set_label_text_if_changed(s_settings_rows[row], text);
        const bool is_group = item.kind == UI_SETTINGS_ROW_GROUP;
        // Set per row rather than at creation: which row is a heading changes
        // as groups open and close.
        lv_obj_set_style_text_font(s_settings_rows[row],
                                   is_group ? UI_FONT_TITLE : UI_FONT_BODY, 0);
        lv_obj_set_style_pad_top(s_settings_rows[row], is_group ? 1 : 4, 0);
        const bool selected_row = item.id == selected;
        /* Field tiles darker than they were, for the same reason: the gap to
         * the cursor is what makes it visible, and both ends of it moved. */
        const uint32_t background = selected_row ? UI_COLOR_CURSOR :
            item.kind == UI_SETTINGS_ROW_FIELD ? 0x1D2A36 : UI_COLOR_GROUND;
        // Accent on the cursor, the way every other screen marks its
        // selection: the tile alone reads as a highlight only once you have
        // found it.
        lv_obj_set_style_text_color(s_settings_rows[row],
                                    lv_color_hex(selected_row ? UI_COLOR_ACCENT
                                                              : UI_COLOR_TEXT), 0);
        size_t switch_index = 0U;
        bool enabled = false;
        const bool has_switch = ui_settings_row_switch(item.id, &switch_index, &enabled);
        lv_obj_set_x(s_settings_rows[row], has_switch ? UI_SET_SWITCH_TEXT_X : UI_CONTENT_X);
        lv_obj_set_width(s_settings_rows[row],
                         has_switch ? UI_SET_CHEVRON_X - UI_SET_SWITCH_TEXT_X : UI_CONTENT_W);
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

/* Applies the two flip settings and repaints everything.
 *
 * The repaint is the point. esp_lcd_panel_mirror() changes where the
 * controller puts incoming pixels, and LVGL has no idea it happened: every
 * pixel already on the glass was written under the old mapping and stays
 * exactly where it was, so the screen keeps fragments of the previous
 * orientation until something else happens to invalidate them. On the settings
 * screen that is most of it - only the row under the cursor redraws by itself,
 * and the rest of the list sits there mirrored. */
static void ui_apply_display_rotation(void)
{
    (void)board_display_set_rotation(s_device_settings.flip_vertical,
                                     s_device_settings.flip_horizontal);
    lv_obj_t *screen = lv_screen_active();
    if (screen != NULL) lv_obj_invalidate(screen);
}

static void ui_show_settings(void)
{
    s_settings_open = true;
    ui_hide_qr();
    ui_settings_model_init(&s_settings_model, ui_home_screen_exists());
    if (!device_settings_init(&s_device_settings)) {
        lv_label_set_text(s_settings_notice, "Ошибка чтения settings.csv");
    } else {
        lv_label_set_text(s_settings_notice, "");
        ui_apply_display_rotation();
        (void)board_backlight_set(s_device_settings.brightness);
    }
    ui_apply_yandex_visibility();
    device_settings_publish(&s_device_settings);
    ui_update_settings();
    lv_screen_load(s_settings_screen);
}

static void ui_close_settings(void)
{
    if (!s_settings_open) return;
    s_settings_open = false;
    ui_hide_qr();
    /* Disarmed on the way out: coming back to a screen whose knob still edits
     * the row it was left on is the sort of thing nobody expects. */
    s_settings_model.editing = false;
    ui_load_menu_screen();
}

/* Picks up a settings change made somewhere other than this task - today the
 * web interface, which writes settings.csv through the same setters and then
 * raises the flag this reads.
 *
 * Re-reading the file is the easy half. The rest is what a stored value alone
 * does not do: the backlight, the panel rotation, the output volume and the
 * Yandex row in both home screens all have to be told, and this task is the
 * only one allowed to tell them. */
static void ui_reload_settings(void)
{
    /* A knob being turned right now has not reached the file yet - the write
     * waits for the turn to settle - so the value on the card is the one from
     * before it, and re-reading would undo the turn under the user's hand. */
    const bool volume_pending = s_volume_save_pending;
    const bool brightness_pending = s_brightness_save_pending;
    const unsigned char turning_volume = board_audio_volume();
    const unsigned char turning_brightness = s_device_settings.brightness;

    if (!device_settings_init(&s_device_settings)) {
        lv_label_set_text(s_settings_notice, "Ошибка чтения settings.csv");
        return;
    }
    if (volume_pending) s_device_settings.volume = turning_volume;
    if (brightness_pending) s_device_settings.brightness = turning_brightness;

    ui_apply_display_rotation();
    (void)board_backlight_set(s_device_settings.brightness);
    if (!volume_pending) board_audio_set_volume(s_device_settings.volume);
    ui_apply_yandex_visibility();
    /* The model is left alone while the settings screen is open: re-initialising
     * it moves the cursor back to the top, and someone standing at the device
     * has not asked for that. Its idea of whether a home screen exists can then
     * be one Yandex switch out of date until the screen is left and reopened,
     * which is the narrower of the two problems. */
    device_settings_publish(&s_device_settings);
    if (s_settings_open) {
        ui_update_settings();
    } else {
        ui_settings_model_init(&s_settings_model, ui_home_screen_exists());
    }
}

/* Yandex Music pairing screen. It exists because the device flow needs a place
 * to print the code and the address the user types it into: the code is
 * useless in the serial log, which nobody has open while linking a phone. */
static void ui_create_yandex_screen(void)
{
    s_yandex_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_yandex_screen, lv_color_hex(UI_COLOR_GROUND), 0);
    lv_obj_set_style_border_width(s_yandex_screen, 0, 0);
    lv_obj_set_style_pad_all(s_yandex_screen, 0, 0);
    lv_obj_set_style_text_font(s_yandex_screen, UI_FONT_BODY, 0);

    /* The same strip the other lists carry, rather than a bare heading: the
     * clock and the signal belong on every screen the user can sit on, and
     * this one is sat on for as long as a pairing code lasts. */
    ui_status_strip_create(s_yandex_screen, &s_yandex_strip, "ЯМузыка");

    s_yandex_status = lv_label_create(s_yandex_screen);
    lv_obj_set_pos(s_yandex_status, 12, 44);
    lv_obj_set_width(s_yandex_status, TFT_WIDTH - 24);
    lv_label_set_long_mode(s_yandex_status, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_yandex_status, lv_color_hex(UI_COLOR_MUTED), 0);
    lv_label_set_text(s_yandex_status, "");

    /* A panel rather than three loose labels: the block appears and disappears
     * as a unit, and hiding one parent is both cheaper and impossible to get
     * half right. */
    s_yandex_code_panel = lv_obj_create(s_yandex_screen);
    lv_obj_remove_style_all(s_yandex_code_panel);
    lv_obj_set_pos(s_yandex_code_panel, 12, 70);
    lv_obj_set_size(s_yandex_code_panel, TFT_WIDTH - 24, 122);
    lv_obj_set_style_bg_color(s_yandex_code_panel, lv_color_hex(UI_COLOR_STRIP), 0);
    lv_obj_set_style_bg_opa(s_yandex_code_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_yandex_code_panel, 8, 0);
    lv_obj_add_flag(s_yandex_code_panel, LV_OBJ_FLAG_HIDDEN);

    /* Montserrat rather than the Cyrillic face the rest of the screen uses:
     * the pairing code is always ASCII, and this is the largest font already
     * linked into the image (the player's placeholder art uses it), so reading
     * it from across the room costs no extra flash. Truncates rather than
     * spills if a code is ever longer than the eight characters Yandex
     * issues. */
    s_yandex_code = lv_label_create(s_yandex_code_panel);
    lv_obj_set_pos(s_yandex_code, 0, 6);
    lv_obj_set_width(s_yandex_code, TFT_WIDTH - 24);
    lv_label_set_long_mode(s_yandex_code, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_yandex_code, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_yandex_code, UI_FONT_DISPLAY, 0);
    lv_obj_set_style_text_color(s_yandex_code, lv_color_hex(UI_COLOR_ACCENT), 0);
    lv_label_set_text(s_yandex_code, "");

    /* Bigger than the body text, smaller than the code. The address is ASCII
     * too, so it takes the same Montserrat family - and both sizes are already
     * linked, so neither costs flash. */
    s_yandex_url = lv_label_create(s_yandex_code_panel);
    lv_obj_set_pos(s_yandex_url, 0, 66);
    lv_obj_set_width(s_yandex_url, TFT_WIDTH - 24);
    lv_label_set_long_mode(s_yandex_url, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_yandex_url, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_yandex_url, UI_FONT_ICON, 0);
    lv_obj_set_style_text_color(s_yandex_url, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_label_set_text(s_yandex_url, "");

    s_yandex_countdown = lv_label_create(s_yandex_code_panel);
    lv_obj_set_pos(s_yandex_countdown, 0, 98);
    lv_obj_set_style_text_font(s_yandex_countdown, UI_FONT_BODY, 0);
    lv_obj_set_width(s_yandex_countdown, TFT_WIDTH - 24);
    lv_obj_set_style_text_align(s_yandex_countdown, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_yandex_countdown, lv_color_hex(UI_COLOR_DIM), 0);
    lv_label_set_text(s_yandex_countdown, "");

    s_yandex_rule = lv_obj_create(s_yandex_screen);
    lv_obj_set_pos(s_yandex_rule, 10, UI_LIST_RULE_Y);
    lv_obj_set_size(s_yandex_rule, UI_CONTENT_W, 1);
    lv_obj_set_style_bg_color(s_yandex_rule, lv_color_hex(UI_COLOR_RULE), 0);
    lv_obj_set_style_border_width(s_yandex_rule, 0, 0);
    lv_obj_set_style_pad_all(s_yandex_rule, 0, 0);
    lv_obj_clear_flag(s_yandex_rule, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_yandex_rule, LV_OBJ_FLAG_HIDDEN);

    s_yandex_progress = lv_bar_create(s_yandex_screen);
    lv_obj_set_pos(s_yandex_progress, 10, UI_LIST_PROGRESS_Y);
    lv_obj_set_size(s_yandex_progress, UI_CONTENT_W, 4);
    lv_bar_set_range(s_yandex_progress, 0, 100);
    lv_obj_set_style_radius(s_yandex_progress, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(s_yandex_progress, 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_yandex_progress, lv_color_hex(0x23303C), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_yandex_progress, lv_color_hex(UI_COLOR_ACCENT),
                              LV_PART_INDICATOR);
    lv_obj_add_flag(s_yandex_progress, LV_OBJ_FLAG_HIDDEN);

    for (size_t row = 0; row < UI_YANDEX_LIST_ROWS; ++row) {
        s_yandex_rows[row] = ui_scroller_create(
            s_yandex_screen, UI_CONTENT_X, UI_LIST_ROW_Y + (int)row * UI_LIST_ROW_PITCH,
            UI_CONTENT_W, UI_LIST_ROW_H);
        lv_obj_set_style_text_font(s_yandex_rows[row].box, UI_FONT_TITLE, 0);
        lv_obj_set_style_pad_left(s_yandex_rows[row].box, 8, 0);
        lv_obj_set_style_pad_top(s_yandex_rows[row].box, 3, 0);
        lv_obj_set_style_radius(s_yandex_rows[row].box, 3, 0);
        lv_obj_set_style_bg_opa(s_yandex_rows[row].box, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(s_yandex_rows[row].box, lv_color_hex(UI_COLOR_GROUND), 0);
        lv_obj_add_flag(s_yandex_rows[row].box, LV_OBJ_FLAG_HIDDEN);
    }

    /* After the rows, for the reason the file browser's notice is: the rows
     * are opaque and LVGL paints children in creation order, so a message
     * created first is painted underneath a screen of blank rows. */
    s_yandex_message_icon = lv_image_create(s_yandex_screen);
    lv_obj_remove_style_all(s_yandex_message_icon);
    lv_image_set_src(s_yandex_message_icon,
                     ui_feed_icon_image(UI_FEED_YANDEX, UI_FEED_ICON_LARGE));
    lv_obj_set_size(s_yandex_message_icon, UI_LIST_NOTICE_ICON, UI_LIST_NOTICE_ICON);
    lv_image_set_inner_align(s_yandex_message_icon, LV_IMAGE_ALIGN_CENTER);
    lv_obj_set_pos(s_yandex_message_icon, (TFT_WIDTH - UI_LIST_NOTICE_ICON) / 2,
                   UI_LIST_NOTICE_ICON_Y);
    lv_obj_set_style_image_recolor(s_yandex_message_icon, lv_color_hex(UI_COLOR_NOTICE), 0);
    lv_obj_set_style_image_recolor_opa(s_yandex_message_icon, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_yandex_message_icon, LV_OBJ_FLAG_HIDDEN);

    s_yandex_message = lv_label_create(s_yandex_screen);
    lv_label_set_text(s_yandex_message, "");
    lv_obj_set_pos(s_yandex_message, 10, UI_LIST_NOTICE_TEXT_Y);
    lv_obj_set_width(s_yandex_message, UI_CONTENT_W);
    lv_obj_set_style_text_align(s_yandex_message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_yandex_message, UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(s_yandex_message, lv_color_hex(UI_COLOR_NOTICE), 0);

    /* Only the screens with room for it keep a hint line: in list mode the
     * rule and the bar have that space, and the two lists it now matches
     * name no keys either. It still carries the passing notice, which is why
     * it is created after the rows rather than beside the status line. */
    s_yandex_hint = lv_label_create(s_yandex_screen);
    lv_obj_set_pos(s_yandex_hint, 12, TFT_HEIGHT - 28);
    lv_obj_set_width(s_yandex_hint, TFT_WIDTH - 24);
    lv_label_set_long_mode(s_yandex_hint, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_yandex_hint, lv_color_hex(UI_COLOR_DIM), 0);
    lv_label_set_text(s_yandex_hint, "");
}

/* Draws the station rows. Separate from the update below because setting a
 * label's text restarts its scroll animation: called every pass of the poll
 * loop, the selected row would never finish scrolling its name. */
static void ui_update_yandex_rows(void)
{
    size_t cursor_row = 0U;
    const int count = (int)s_yandex_list.count;
    const int window_top = station_list_window_top(&s_yandex_list, UI_YANDEX_LIST_ROWS,
                                                   &cursor_row);
    for (size_t row = 0; row < UI_YANDEX_LIST_ROWS; ++row) {
        const int entry_index = window_top + (int)row;
        yandex_station_t station;
        if (entry_index < 0 || entry_index >= count ||
            !yandex_catalog_station_at((size_t)entry_index, &station)) {
            lv_obj_add_flag(s_yandex_rows[row].box, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(s_yandex_rows[row].box, LV_OBJ_FLAG_HIDDEN);
        const bool selected = row == cursor_row;
        const bool active = (size_t)entry_index == station_list_active_index(&s_yandex_list);
        lv_obj_set_style_bg_color(s_yandex_rows[row].box,
                                  lv_color_hex(selected ? UI_COLOR_SELECTED
                                                        : UI_COLOR_GROUND), 0);
        // The station list's three brightnesses, for the same reason: the
        // cursor takes the accent, the station that is playing is the
        // brightest of the rest, everything else stays muted.
        lv_obj_set_style_text_color(s_yandex_rows[row].box,
                                    lv_color_hex(selected ? UI_COLOR_ACCENT
                                                 : active ? UI_COLOR_TEXT
                                                          : UI_COLOR_MUTED), 0);
        // Only the row being pointed at scrolls its full name; a screen of
        // marquees is unreadable, which is the same call the station list made.
        ui_scroller_set_scrolling(&s_yandex_rows[row], selected);
        ui_scroller_set_text(&s_yandex_rows[row], station.name);
    }
    lv_bar_set_value(s_yandex_progress, station_list_progress_percent(&s_yandex_list),
                     LV_ANIM_OFF);
}

/* Which row is playing, or `count` for none. Asked of the player's own view
 * state rather than remembered here, so the mark follows a station started
 * from the web UI as readily as one started from this screen. */
static size_t ui_yandex_active_index(size_t count)
{
    if (ui_player_state_source(&s_player_ui) != AUDIO_SOURCE_YANDEX) return count;
    const size_t active = ui_player_state_active_item(&s_player_ui);
    return active < count ? active : count;
}

/* The rule and the position bar, which belong to the rows rather than to the
 * screen: under a message saying there are no stations they would be pointing
 * at nothing, and they also step aside for the few seconds a passing notice
 * needs, because the notice is drawn on the line they occupy. */
static void ui_yandex_show_frame(bool shown)
{
    if (shown) {
        lv_obj_remove_flag(s_yandex_rule, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_yandex_progress, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_yandex_rule, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_yandex_progress, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ui_hide_yandex_rows(void)
{
    for (size_t row = 0; row < UI_YANDEX_LIST_ROWS; ++row) {
        lv_obj_add_flag(s_yandex_rows[row].box, LV_OBJ_FLAG_HIDDEN);
    }
    ui_yandex_show_frame(false);
    s_yandex_rows_drawn = false;
}

static void ui_yandex_show_notice(const char *text)
{
    s_yandex_notice = text;
    s_yandex_notice_until_ms = ui_tick_get_ms() + UI_YANDEX_NOTICE_MS;
}

static void ui_update_yandex(void)
{
    if (!s_yandex_open) return;
    const yandex_auth_status_t status = yandex_auth_get_status();
    const yandex_catalog_state_t catalog_state = yandex_catalog_get_state();
    const size_t count = yandex_catalog_count();
    ui_yandex_view_t view;
    ui_yandex_view_build(&status, catalog_state, count, &view);

    s_yandex_mode = view.mode;

    /* A transient answer to a press - "that button does nothing yet" - takes
     * the hint line for a few seconds, because there is nowhere else on this
     * screen that a message belongs while the rows fill it. */
    const uint32_t now_ms = ui_tick_get_ms();
    const bool notice_live = s_yandex_notice != NULL &&
                             (int32_t)(s_yandex_notice_until_ms - now_ms) > 0;
    if (!notice_live) s_yandex_notice = NULL;

    const bool message = view.mode == UI_YANDEX_MODE_MESSAGE;
    /* Two places one sentence can go. While the account is being linked it is
     * a caption above the code; with nothing to list it is the whole point of
     * the screen, and takes the shape the file browser gives that case. */
    ui_set_label_text_if_changed(s_yandex_status, message ? "" : view.status);
    ui_set_label_text_if_changed(s_yandex_message, message ? view.status : "");
    if (message) {
        lv_obj_remove_flag(s_yandex_message_icon, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_yandex_message_icon, LV_OBJ_FLAG_HIDDEN);
    }
    /* The keys are named only where there is room to name them. In list mode
     * the line belongs to the rule and the bar, the way it does on the other
     * two lists - so nothing is said there unless something went wrong. */
    const bool list = view.mode == UI_YANDEX_MODE_LIST;
    ui_set_label_text_if_changed(s_yandex_hint, notice_live ? s_yandex_notice
                                                : list      ? ""
                                                            : view.hint);

    if (view.show_code) {
        ui_set_label_text_if_changed(s_yandex_code, view.code);
        ui_set_label_text_if_changed(s_yandex_url, view.url);
        ui_set_label_text_if_changed(s_yandex_countdown, view.countdown);
        lv_obj_remove_flag(s_yandex_code_panel, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_yandex_code_panel, LV_OBJ_FLAG_HIDDEN);
    }

    if (!list) {
        if (s_yandex_rows_drawn) ui_hide_yandex_rows();
        return;
    }
    ui_yandex_show_frame(!notice_live);
    /* The station that is playing, not "none". Passing the count here - which
     * is what this call used to do - overwrote the active row the moment the
     * screen was opened, so the mark ui_show_yandex sets survived exactly one
     * pass of the poll loop and the playing station looked like any other. */
    (void)station_list_sync_counts(&s_yandex_list, count, ui_yandex_active_index(count));
    const unsigned int revision = yandex_catalog_revision();
    const size_t selected = station_list_selected_index(&s_yandex_list);
    const size_t drawn_active = station_list_active_index(&s_yandex_list);
    if (s_yandex_rows_drawn && revision == s_yandex_drawn_revision &&
        selected == s_yandex_drawn_selected && count == s_yandex_drawn_count &&
        drawn_active == s_yandex_drawn_active) {
        return;
    }
    s_yandex_drawn_revision = revision;
    s_yandex_drawn_selected = selected;
    s_yandex_drawn_count = count;
    s_yandex_drawn_active = drawn_active;
    s_yandex_rows_drawn = true;
    ui_update_yandex_rows();
}

static void ui_show_yandex(void)
{
    s_yandex_open = true;
    s_yandex_notice = NULL;
    ui_hide_yandex_rows();
    /* Opening the screen with no account starts the exchange straight away:
     * the alternative is a screen that says "not linked" and needs a second
     * press to do the only thing it offers. */
    if (!yandex_auth_is_authorized()) {
        (void)yandex_auth_begin();
    } else {
        /* Fetched on every visit rather than cached for the session: the
         * dashboard is personalised and the device may have been sitting on
         * this screen's stale copy for days. It costs one request, and an
         * answer that changed nothing does not disturb the rows. */
        const size_t count = yandex_catalog_count();
        /* Opened over a playing station - the double click from the player -
         * the cursor starts on it and the row is marked, the same thing the
         * radio list does. Opened from the home screen there is nothing here
         * to point at yet. */
        const size_t active = ui_yandex_active_index(count);
        station_list_init(&s_yandex_list, count, active < count ? active : 0U, active);
        (void)yandex_catalog_request_refresh();
    }
    /* Starts the return timer, and starts it here rather than only on the
     * first turn of the knob: a screen opened by accident has to find its own
     * way back too. */
    station_list_note_activity(&s_yandex_list, ui_tick_get_ms());
    ui_update_yandex();
    lv_screen_load(s_yandex_screen);
}

static void ui_close_yandex(void)
{
    if (!s_yandex_open) return;
    /* Leaving the screen abandons an unfinished attempt rather than leaving a
     * task polling for a code nobody can see any more. */
    (void)yandex_auth_cancel();
    s_yandex_start_row = PLAYER_ITEM_NONE;
    s_yandex_open = false;
    /* Back to whatever this screen was opened over. Opened from the home
     * screen the view is still MENU and this loads it; opened with a double
     * click from a playing station it is SOURCE, and dropping to the menu
     * there would throw away the player screen the user was on - and leave the
     * view state saying "player" while the menu is drawn, which binds every
     * button to the wrong screen. */
    ui_render_player_state();
}

/* The list belonging to `source`. Yandex has a screen of its own - its rows
 * are fetched from the account rather than read from the catalog file - so
 * the double click that opens a list has to pick between the two. */
static void ui_open_source_list(audio_source_t source)
{
    if (source == AUDIO_SOURCE_YANDEX) {
        ui_show_yandex();
        return;
    }
    ui_show_station_list();
}

/* The like key's two gestures, resolved against the mark the track carries.
 *
 * A double press asks for the rejection. A single one normally asks for the
 * like - but on a track already rejected it takes that back instead, because
 * this is the only key on the box that can and turning a rejection straight
 * into a like is not a gesture anybody made. Which is why this reads the
 * snapshot rather than sending a fixed command: the same press means two
 * different things depending on what the track is already marked with. */
static void ui_submit_like_press(bool double_press)
{
    player_snapshot_t snapshot;
    player_control_get_snapshot(&snapshot);
    player_command_t command = {
        .kind = PLAYER_COMMAND_TOGGLE_LIKE,
        .source = AUDIO_SOURCE_YANDEX,
        .item_index = PLAYER_ITEM_NONE,
    };
    if (double_press || snapshot.track_disliked) {
        command.kind = PLAYER_COMMAND_TOGGLE_DISLIKE;
    }
    (void)ui_submit_player_command(&command);
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
    case UI_SETTINGS_ROW_SCROLL_FIELD:
        changed = device_settings_set_scroll(&s_device_settings,
                                             s_device_settings.scroll == DEVICE_SCROLL_LEFT
                                                 ? DEVICE_SCROLL_BOUNCE
                                                 : DEVICE_SCROLL_LEFT);
        break;
    case UI_SETTINGS_ROW_BUFFER_FIELD:
        changed = device_settings_set_buffer_view(
            &s_device_settings,
            s_device_settings.buffer_view == DEVICE_BUFFER_VIEW_GRAPH
                ? DEVICE_BUFFER_VIEW_TEXT
                : DEVICE_BUFFER_VIEW_GRAPH);
        /* The strip starts empty whichever way the switch went: coming back to
         * one holding a station that stopped being watched minutes ago says
         * nothing about the one playing now. */
        if (changed) ui_buffer_graph_reset(&s_buffer_graph, ui_tick_get_ms());
        break;
    case UI_SETTINGS_ROW_AUTOPLAY_FIELD:
        changed = device_settings_set_autoplay(&s_device_settings,
                                               !s_device_settings.autoplay);
        break;
    case UI_SETTINGS_ROW_YANDEX_FIELD:
        changed = device_settings_set_yandex_music(&s_device_settings,
                                                   !s_device_settings.yandex_music);
        if (changed) ui_apply_yandex_visibility();
        break;
    case UI_SETTINGS_ROW_FLIP_VERTICAL_FIELD:
        changed = device_settings_set_flip_vertical(&s_device_settings,
                                                    !s_device_settings.flip_vertical);
        if (changed) ui_apply_display_rotation();
        break;
    case UI_SETTINGS_ROW_FLIP_HORIZONTAL_FIELD:
        changed = device_settings_set_flip_horizontal(&s_device_settings,
                                                      !s_device_settings.flip_horizontal);
        if (changed) ui_apply_display_rotation();
        break;
    default:
        return;
    }
    lv_label_set_text(s_settings_notice, changed ? "" : "Ошибка записи settings.csv");
    // Whatever the card said, this task's copy is what the web has to show.
    if (changed) device_settings_publish(&s_device_settings);
}

/* The number fields. Separate from ui_settings_change_selected() because a
 * click and a detent mean different things here: the click only decides who
 * the knob belongs to, and this is the turn that moves the value. */
static void ui_settings_change_number(int direction)
{
    if (ui_settings_model_selected(&s_settings_model) != UI_SETTINGS_ROW_BRIGHTNESS_FIELD) {
        return;
    }
    const int next = ui_settings_brightness_step((int)s_device_settings.brightness, direction);
    if (next == (int)s_device_settings.brightness) return;
    /* Straight to the panel, saved later. Writing settings.csv per detent is
     * the same read-modify-write that made the volume knob queue up clicks,
     * and here the lag would be visible as well as felt. */
    s_device_settings.brightness = (unsigned char)next;
    (void)board_backlight_set(s_device_settings.brightness);
    s_brightness_save_pending = true;
    s_brightness_changed_ms = ui_tick_get_ms();
    device_settings_publish(&s_device_settings);
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
    lv_obj_set_style_text_font(s_station_list_screen, UI_FONT_BODY, 0);
    ui_status_strip_create(s_station_list_screen, &s_list_strip, "");
    /* The heading moves into the strip's left slot instead of taking a line of
     * its own - which is where the room for the rule and the position bar came
     * from, without giving up a row of the list. */
    s_station_list_title = s_list_strip.context;

    s_station_list_rule = lv_obj_create(s_station_list_screen);
    lv_obj_set_pos(s_station_list_rule, 10, UI_LIST_RULE_Y);
    lv_obj_set_size(s_station_list_rule, UI_CONTENT_W, 1);
    lv_obj_set_style_bg_color(s_station_list_rule, lv_color_hex(UI_COLOR_RULE), 0);
    lv_obj_set_style_border_width(s_station_list_rule, 0, 0);
    lv_obj_set_style_pad_all(s_station_list_rule, 0, 0);
    lv_obj_clear_flag(s_station_list_rule, LV_OBJ_FLAG_SCROLLABLE);

    // Same shape and colours as the track bar on the player screen: a thin
    // full-width line whose filled part is the accent.
    s_station_list_progress = lv_bar_create(s_station_list_screen);
    lv_obj_set_pos(s_station_list_progress, 10, UI_LIST_PROGRESS_Y);
    lv_obj_set_size(s_station_list_progress, UI_CONTENT_W, 4);
    lv_bar_set_range(s_station_list_progress, 0, 100);
    lv_obj_set_style_radius(s_station_list_progress, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(s_station_list_progress, 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_station_list_progress, lv_color_hex(0x23303C), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_station_list_progress, lv_color_hex(UI_COLOR_ACCENT),
                              LV_PART_INDICATOR);
    for (size_t row = 0; row < UI_STATION_LIST_MAX_ROWS; ++row) {
        /* The box is the row, and its width is the panel's - not a 300 typed
         * when there was only one panel. It is what the marquee measures
         * itself against: too wide, and a name that overflows the screen is
         * calculated to fit and never travels, while one that does travel
         * stops with its tail still off the edge. */
        s_station_list_rows[row] = ui_scroller_create(s_station_list_screen, UI_CONTENT_X,
                                                     UI_LIST_ROW_Y + (int)row * UI_LIST_ROW_PITCH,
                                                     UI_CONTENT_W, UI_LIST_ROW_H);
        // Bigger than the rest of the screen on purpose: this is the text the
        // user reads from a distance while turning the encoder. The 23 px line
        // it needs is what set the row height and the pitch above.
        lv_obj_set_style_text_font(s_station_list_rows[row].box, UI_FONT_TITLE, 0);
        lv_obj_set_style_pad_left(s_station_list_rows[row].box, 8, 0);
        lv_obj_set_style_pad_top(s_station_list_rows[row].box, 3, 0);
        lv_obj_set_style_radius(s_station_list_rows[row].box, 3, 0);
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
        lv_obj_set_style_text_font(s_station_list_icons[row], UI_FONT_ICON, 0);
        lv_obj_set_style_text_color(s_station_list_icons[row],
                                    lv_color_hex(UI_COLOR_FOLDER), 0);
        // Which glyph a row carries is decided as the row is drawn - a folder
        // and a playlist do not get the same one - so the label starts empty.
        lv_label_set_text(s_station_list_icons[row], "");
        lv_obj_add_flag(s_station_list_icons[row], LV_OBJ_FLAG_HIDDEN);
    }
    /* Same reason as the marks above: created after the rows so it draws over
     * the opaque row background, and at the same left edge and baseline the
     * name would have had without it. */
    for (size_t row = 0; row < UI_STATION_LIST_MAX_ROWS; ++row) {
        s_station_list_numbers[row] = lv_label_create(s_station_list_screen);
        /* The whole column, not just the digits, and opaque in the row's own
         * colour. A scrolling name travels left out of the box's padding and
         * LVGL clips it at the box edge, not at the padding - so without
         * something solid over the column the text slides across the number
         * and the two are read together. This is that something: same left
         * edge and same corner radius as the row, so it disappears into it. */
        lv_obj_set_pos(s_station_list_numbers[row], UI_CONTENT_X,
                       UI_LIST_ROW_Y + (int)row * UI_LIST_ROW_PITCH);
        lv_obj_set_size(s_station_list_numbers[row], 8 + UI_LIST_NUMBER_W, UI_LIST_ROW_H);
        lv_obj_set_style_pad_left(s_station_list_numbers[row], 8, 0);
        lv_obj_set_style_pad_top(s_station_list_numbers[row], 3, 0);
        lv_obj_set_style_radius(s_station_list_numbers[row], 3, 0);
        lv_obj_set_style_bg_opa(s_station_list_numbers[row], LV_OPA_COVER, 0);
        lv_obj_set_style_text_font(s_station_list_numbers[row], UI_FONT_TITLE, 0);
        lv_label_set_text(s_station_list_numbers[row], "");
        lv_obj_add_flag(s_station_list_numbers[row], LV_OBJ_FLAG_HIDDEN);
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
    lv_obj_set_pos(s_station_list_notice_icon, (TFT_WIDTH - UI_LIST_NOTICE_ICON) / 2,
                   UI_LIST_NOTICE_ICON_Y);
    lv_obj_set_style_image_recolor(s_station_list_notice_icon,
                                   lv_color_hex(UI_COLOR_NOTICE), 0);
    lv_obj_set_style_image_recolor_opa(s_station_list_notice_icon, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_station_list_notice_icon, LV_OBJ_FLAG_HIDDEN);

    s_station_list_notice = lv_label_create(s_station_list_screen);
    lv_label_set_text(s_station_list_notice, "");
    lv_obj_set_pos(s_station_list_notice, 10, UI_LIST_NOTICE_TEXT_Y);
    lv_obj_set_width(s_station_list_notice, UI_CONTENT_W);
    // Centred under the drive, and wrapped rather than ellipsised: the
    // unreadable-drive line is two lines wide and its second half - the format
    // to use - is the half worth reading.
    lv_obj_set_style_text_align(s_station_list_notice, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_station_list_notice, UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(s_station_list_notice, lv_color_hex(UI_COLOR_NOTICE), 0);
}

static void ui_show_station_list(void);
// Defined with the rest of the scrubbing mode, below the command helpers it
// needs; every way off the player screen has to close the mode first.
static void ui_end_seek(void);


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
    lv_obj_set_style_text_font(s_source_screen, UI_FONT_BODY, 0);

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
    lv_obj_set_style_text_font(s_source_art, UI_FONT_DISPLAY, 0);
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
    lv_obj_set_pos(s_source_title, UI_SRC_TEXT_X, UI_SRC_ROW_TITLE);
    lv_obj_set_size(s_source_title, UI_SRC_TEXT_W, UI_SRC_LINE_H);
    lv_label_set_long_mode(s_source_title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_source_title, lv_color_hex(UI_COLOR_ACCENT), 0);

    s_source_detail = ui_scroller_create(s_source_screen, UI_SRC_TEXT_X, UI_SRC_ROW_TRACK,
                                        UI_SRC_TEXT_W, UI_SRC_TRACK_H);
    lv_obj_set_style_text_font(s_source_detail.box, UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(s_source_detail.box, lv_color_hex(UI_COLOR_TEXT), 0);
    /* The track name always travels when it is too long - unlike a list row,
     * there is nothing else on this screen competing for the eye. */
    ui_scroller_set_scrolling(&s_source_detail, true);

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

#if UI_SRC_TEXT_CENTRED
    /* Centred as a column under a centred cover. A layout that reads the three
     * rows down their left edge beside the tile sets nothing. */
    lv_obj_set_style_text_align(s_source_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_align(s_source_artist, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_align(s_source_status, LV_TEXT_ALIGN_CENTER, 0);
    s_source_detail.centred = true;
#endif

    /* Not centred with the three rows below, on either panel: here it is a
     * column of readings against the left margin, there a line under the
     * performer that starts where the performer does. */
    s_source_stream = lv_label_create(s_source_screen);
    lv_obj_set_pos(s_source_stream, UI_SRC_STREAM_X, UI_SRC_STREAM_Y);
    lv_obj_set_size(s_source_stream, UI_SRC_STREAM_W, UI_SRC_STREAM_H);
    lv_label_set_long_mode(s_source_stream, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_source_stream, lv_color_hex(UI_COLOR_DIM), 0);

    /* Brighter than the meter's own unlit blocks, which looks backwards for a
     * divider until you remember it is one pixel tall: a hairline loses far
     * more perceived contrast than a solid block of the same colour, and at
     * 0x23303C these were there in principle and invisible in practice. */
    lv_obj_t *rule_top = lv_obj_create(s_source_screen);
    lv_obj_set_pos(rule_top, 10, UI_SRC_RULE_TOP);
    lv_obj_set_size(rule_top, UI_CONTENT_W, 1);
    lv_obj_set_style_bg_color(rule_top, lv_color_hex(UI_COLOR_RULE), 0);
    lv_obj_set_style_border_width(rule_top, 0, 0);
    lv_obj_set_style_pad_all(rule_top, 0, 0);
    lv_obj_clear_flag(rule_top, LV_OBJ_FLAG_SCROLLABLE);

    for (size_t channel = 0; channel < 2U; ++channel) {
        for (size_t segment = 0; segment < UI_VU_SEGMENTS; ++segment) {
            lv_obj_t *block = lv_obj_create(s_source_screen);
            lv_obj_set_size(block, UI_VU_SEGMENT_W, UI_SRC_VU_BLOCK_H);
            lv_obj_set_pos(block,
                           UI_SRC_VU_X + (int)segment * (UI_VU_SEGMENT_W + UI_VU_SEGMENT_GAP),
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
        lv_obj_set_pos(mark, UI_CONTENT_X + 2, UI_SRC_VU_Y - 3 + (int)channel * UI_SRC_VU_PITCH);
        lv_obj_set_style_text_color(mark, lv_color_hex(UI_COLOR_DIM), 0);
    }

    lv_obj_t *rule_bottom = lv_obj_create(s_source_screen);
    lv_obj_set_pos(rule_bottom, 10, UI_SRC_RULE_BOTTOM);
    lv_obj_set_size(rule_bottom, UI_CONTENT_W, 1);
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
    lv_obj_set_size(s_source_progress, UI_CONTENT_W, UI_SRC_PROGRESS_H);
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

    /* The same reading as a strip, in the same place and about the same size,
     * on a ground a shade lighter than the screen so the empty part of it is
     * still a shape rather than a hole. Hidden until the setting asks for it,
     * and until the footer's left slot is the buffer's to use. */
    s_source_buffer_graph = lv_obj_create(s_source_screen);
    lv_obj_set_pos(s_source_buffer_graph, UI_CONTENT_X, UI_SRC_BUFFER_GRAPH_Y);
    lv_obj_set_size(s_source_buffer_graph, UI_BUFFER_GRAPH_W, UI_SRC_BUFFER_GRAPH_H);
    lv_obj_set_style_bg_color(s_source_buffer_graph, lv_color_hex(UI_COLOR_TILE), 0);
    lv_obj_set_style_border_width(s_source_buffer_graph, 0, 0);
    lv_obj_set_style_radius(s_source_buffer_graph, 2, 0);
    lv_obj_set_style_pad_all(s_source_buffer_graph, 0, 0);
    lv_obj_clear_flag(s_source_buffer_graph, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_source_buffer_graph, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_source_buffer_graph, ui_buffer_graph_draw,
                        LV_EVENT_DRAW_MAIN_END, NULL);
    /* Armed against the clock rather than left at zero: a strip whose deadline
     * is in the past takes its first bar on the very first poll, which is the
     * device still booting and no station open. */
    ui_buffer_graph_reset(&s_buffer_graph, ui_tick_get_ms());

    /* Between the two readings, in the gap the buffer line does not reach: the
     * footer is where the things you can do to what is playing live, and the
     * volume moved right to make room rather than the mark being pushed out to
     * an edge on its own. */
    s_source_like = lv_image_create(s_source_screen);
    lv_obj_remove_style_all(s_source_like);
    lv_image_set_src(s_source_like, &ui_feed_icon_heart_16);
    lv_obj_set_size(s_source_like, 16, 16);
    lv_obj_set_pos(s_source_like, UI_SRC_LIKE_X, UI_SRC_FOOT_Y + 1);
    lv_obj_set_style_image_recolor_opa(s_source_like, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_source_like, LV_OBJ_FLAG_HIDDEN);

    /* Names the bar next to it. Without it the strip was just a rectangle in
     * the footer, indistinguishable from the progress bar above. Centred on
     * the bar's own middle line rather than on the footer row. */
    s_source_volume_icon = lv_image_create(s_source_screen);
    lv_obj_remove_style_all(s_source_volume_icon);
    lv_image_set_src(s_source_volume_icon, &ui_feed_icon_volume_16);
    lv_obj_set_size(s_source_volume_icon, 16, 16);
    lv_obj_set_pos(s_source_volume_icon, UI_SRC_VOLUME_ICON_X, UI_SRC_FOOT_Y + 1);
    lv_obj_set_style_image_recolor(s_source_volume_icon, lv_color_hex(UI_COLOR_MUTED), 0);
    lv_obj_set_style_image_recolor_opa(s_source_volume_icon, LV_OPA_COVER, 0);

    s_source_volume_bar = lv_obj_create(s_source_screen);
    lv_obj_set_pos(s_source_volume_bar, UI_SRC_VOLUME_BAR_X, UI_SRC_FOOT_Y + 5);
    lv_obj_set_size(s_source_volume_bar, UI_SRC_VOLUME_BAR_W, 8);
    lv_obj_set_style_bg_color(s_source_volume_bar, lv_color_hex(0x23303C), 0);
    lv_obj_set_style_border_width(s_source_volume_bar, 0, 0);
    lv_obj_set_style_radius(s_source_volume_bar, 2, 0);
    lv_obj_set_style_pad_all(s_source_volume_bar, 0, 0);
    lv_obj_clear_flag(s_source_volume_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *fill = lv_obj_create(s_source_volume_bar);
    lv_obj_set_pos(fill, 0, 0);
    lv_obj_set_size(fill, UI_SRC_VOLUME_BAR_W, 8);
    lv_obj_set_style_bg_color(fill, lv_color_hex(UI_COLOR_MUTED), 0);
    lv_obj_set_style_border_width(fill, 0, 0);
    lv_obj_set_style_radius(fill, 2, 0);
    lv_obj_set_style_pad_all(fill, 0, 0);
    lv_obj_clear_flag(fill, LV_OBJ_FLAG_SCROLLABLE);

    s_source_volume = lv_label_create(s_source_screen);
    lv_obj_set_pos(s_source_volume, UI_SRC_VOLUME_TEXT_X, UI_SRC_FOOT_Y);
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
        ui_set_state_line("Connecting...", "", false);
        ui_scroller_set_text(&s_source_detail, "");
        ui_set_label_text_if_changed(s_source_stream, "");
        s_waiting_for_radio_station = true;
        s_radio_station_wait_started_ms = ui_tick_get_ms();
    } else if (selected_source == AUDIO_SOURCE_YANDEX) {
        /* Connecting like a station, but without the wait that falls back to
         * the radio list: this source has its own screen to fall back to, and
         * its station name is known before the first byte arrives. */
        ui_set_state_line("Connecting...", "", false);
        ui_set_label_text_if_changed(s_source_stream, "");
        s_waiting_for_radio_station = false;
    } else if (audio_source_is_files(selected_source)) {
        s_waiting_for_radio_station = false;
        // Nothing plays until a file is chosen, so this screen opens idle
        // rather than pretending to connect.
        ui_set_state_line("Выберите файл", "", false);
        ui_scroller_set_text(&s_source_detail, "");
        ui_set_label_text_if_changed(s_source_stream, "");
    } else {
        s_waiting_for_radio_station = false;
        ui_set_state_line("Not implemented", "", false);
        ui_scroller_set_text(&s_source_detail, "");
        ui_set_label_text_if_changed(s_source_stream, "");
    }
}

/* The two sources that stream from the internet are refused rather than
 * started when there is no network. A press that did nothing at all would read
 * as a broken button, so it answers on the same notice line that explains a
 * missing drive. */
static bool ui_network_source_blocked(ui_menu_item_t item, lv_obj_t *notice)
{
    if (ui_menu_item_is_enabled(item, ui_menu_yandex_visible(&s_menu),
                                s_last_wifi_connected)) {
        return false;
    }
    ui_set_label_text_if_changed(notice, "Нет сети — см. Настройки");
    return true;
}

static void ui_show_source(void)
{
    const audio_source_t selected_source = ui_menu_activate(&s_menu);
    if (selected_source == AUDIO_SOURCE_NONE) return;
    /* Yandex has its own list screen rather than the shared one: it is fetched
     * from the account, not read from the catalog file. */
    if (selected_source == AUDIO_SOURCE_YANDEX) {
        ui_show_yandex();
        return;
    }
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

/* The radio, opened the way the home screen would have opened it. On a device
 * with no home screen this is where "back" leads: with only two places to be,
 * one of them has to be the resting place, and it is the one that plays.
 *
 * The list rather than the player, because that is where the radio starts from
 * everywhere else - there is nothing to look at on the player screen until a
 * station has been chosen.
 *
 * False when the attempt has to wait. The way back here is out of Settings,
 * and Settings is the one screen the poll loop does not sync the player state
 * on - so the stop that opened it can still be pending, and a select posted
 * while one is in flight is refused. The caller arms a retry; it clears on the
 * next pass, once the snapshot has confirmed the stop. */
static bool ui_open_radio_home(void)
{
    const player_command_t command = {
        .kind = PLAYER_COMMAND_SELECT_SOURCE,
        .source = AUDIO_SOURCE_INTERNET_RADIO,
        .item_index = PLAYER_ITEM_NONE,
    };
    if (!ui_submit_player_command(&command)) return false;
    (void)ui_menu_select_source(&s_menu, AUDIO_SOURCE_INTERNET_RADIO);
    ui_show_station_list();
    return true;
}

static void ui_load_menu_screen(void)
{
    s_waiting_for_radio_station = false;
    if (!ui_home_screen_exists()) {
        s_radio_home_pending = !ui_open_radio_home();
        return;
    }
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
    /* With no home screen this gesture is not a way out but a two-way switch,
     * and this is the half that leads to Settings. It still stops what is
     * playing, which is the point rather than a side effect: the radio is the
     * other half of the switch, and leaving it running behind a screen with no
     * transport controls is how a device ends up playing with no way to say
     * so. */
    if (!ui_home_screen_exists()) {
        ui_show_settings();
        return;
    }
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
    lv_obj_set_size(s_source_progress, UI_CONTENT_W, height);
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

/* Remembers the row OK was pressed on. Starting it takes two commands that
 * cannot be posted together - the source, then the row - because a
 * SELECT_ITEM is only accepted once a snapshot has confirmed the source and
 * the item count that goes with it. So the press records the row and the poll
 * loop drives it, one step per pass. */
static void ui_yandex_start_selected(void)
{
    size_t row;
    if (!station_list_get_selection(&s_yandex_list, &row)) return;
    s_yandex_start_row = row;
    s_yandex_start_deadline_ms = ui_tick_get_ms() + UI_YANDEX_START_TIMEOUT_MS;
}

/* One step of that start, called from the poll loop while the screen is open.
 *
 * The source is taken here rather than when the screen opens, so that looking
 * at the list does not stop whatever is playing - and so that a station can
 * still be started right after the account was linked, when no source could
 * have been selected in advance. */
static void ui_yandex_step_start(const player_snapshot_t *snapshot)
{
    if (s_yandex_start_row == PLAYER_ITEM_NONE) return;
    if ((int32_t)(ui_tick_get_ms() - s_yandex_start_deadline_ms) >= 0) {
        s_yandex_start_row = PLAYER_ITEM_NONE;
        ui_yandex_show_notice("Не удалось запустить станцию");
        return;
    }
    // A command is still on its way; the next pass will find out how it went.
    if (ui_player_state_is_pending(&s_player_ui)) return;

    if (snapshot->active_source != AUDIO_SOURCE_YANDEX) {
        const player_command_t select_source = {
            .kind = PLAYER_COMMAND_SELECT_SOURCE,
            .source = AUDIO_SOURCE_YANDEX,
            .item_index = PLAYER_ITEM_NONE,
        };
        (void)ui_submit_player_command(&select_source);
        return;
    }
    /* The source is up but its list has not reached the snapshot yet. Waiting
     * is right: posting now would be refused for an index the player does not
     * know about. */
    if (s_yandex_start_row >= snapshot->item_count) return;

    const size_t row = s_yandex_start_row;
    const player_command_t start = {
        .kind = PLAYER_COMMAND_SELECT_ITEM,
        .source = AUDIO_SOURCE_YANDEX,
        .item_index = row,
    };
    if (!ui_submit_player_command(&start)) return;
    s_yandex_start_row = PLAYER_ITEM_NONE;
    s_yandex_open = false;
    s_yandex_notice = NULL;
    /* The screen this leaves for is the player, so the view state has to say
     * so: it was never in the player's view machine while this screen was up,
     * and a view left saying "menu" would bind the buttons to the wrong
     * screen. */
    s_player_ui.view = UI_PLAYER_VIEW_SOURCE;
    s_player_ui.source = AUDIO_SOURCE_YANDEX;
    ui_load_source_screen(AUDIO_SOURCE_YANDEX);
    yandex_station_t station;
    if (yandex_catalog_station_at(row, &station)) {
        lv_label_set_text(s_source_title, station.name);
        ui_scroller_set_text(&s_source_detail, station.name);
    }
}

static void ui_handle_input(board_input_action_t action)
{
    // Settings sits outside the player's view state: it shows no source and
    // starts nothing, so making it a fourth view would put an entry in every
    // transition table for no gain. Any of the three ways out returns to the
    // main screen.
    if (s_yandex_open) {
        station_list_note_activity(&s_yandex_list, ui_tick_get_ms());
        if (action == BOARD_INPUT_ACTION_F2 || action == BOARD_INPUT_ACTION_ENCODER_LONG) {
            ui_close_yandex();
        } else if (action == BOARD_INPUT_ACTION_ENCODER_LEFT ||
                   action == BOARD_INPUT_ACTION_ENCODER_RIGHT) {
            if (station_list_handle_input(&s_yandex_list, action)) {
                ui_update_yandex();
            }
        } else if (action == BOARD_INPUT_ACTION_ENCODER_BUTTON) {
            const yandex_auth_status_t status = yandex_auth_get_status();
            ui_yandex_view_t view;
            ui_yandex_view_build(&status, yandex_catalog_get_state(),
                                 yandex_catalog_count(), &view);
            if (view.mode == UI_YANDEX_MODE_PAIRING) {
                /* Only a retry: pressing OK during an attempt would do
                 * nothing, and there is no account to re-link here. */
                if (!ui_yandex_view_is_busy(&status)) (void)yandex_auth_begin();
            } else if (view.mode == UI_YANDEX_MODE_LIST) {
                ui_yandex_start_selected();
            } else if (yandex_catalog_get_state() != YANDEX_CATALOG_LOADING) {
                (void)yandex_catalog_request_refresh();
            }
            ui_update_yandex();
        }
        return;
    }
    if (s_settings_open) {
        if (s_qr_open) {
            /* One thing on screen and one way off it: any button returns, and
             * the knob has nothing to move. */
            if (action == BOARD_INPUT_ACTION_ENCODER_BUTTON ||
                action == BOARD_INPUT_ACTION_ENCODER_LONG ||
                action == BOARD_INPUT_ACTION_F2) {
                ui_hide_qr();
            }
            return;
        }
        if (action == BOARD_INPUT_ACTION_F2 || action == BOARD_INPUT_ACTION_ENCODER_LONG) {
            ui_close_settings();
        } else if (action == BOARD_INPUT_ACTION_ENCODER_LEFT ||
                   action == BOARD_INPUT_ACTION_ENCODER_RIGHT) {
            const int direction = action == BOARD_INPUT_ACTION_ENCODER_RIGHT ? 1 : -1;
            if (ui_settings_model_is_editing(&s_settings_model)) {
                ui_settings_change_number(direction);
            } else {
                (void)ui_settings_model_move(&s_settings_model, direction);
            }
            ui_update_settings();
        } else if (action == BOARD_INPUT_ACTION_ENCODER_BUTTON) {
            const ui_settings_row_t row = ui_settings_model_row_at(
                &s_settings_model, s_settings_model.cursor);
            if (row.kind == UI_SETTINGS_ROW_BAND) {
                ui_show_qr();
            } else if (ui_settings_row_is_number(row.id)) {
                /* The click arms and disarms the knob rather than changing
                 * anything: a number has no next value to step to the way a
                 * switch does. */
                (void)ui_settings_model_toggle_edit(&s_settings_model);
            } else if (row.kind == UI_SETTINGS_ROW_GROUP) {
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
            // Opening a directory or a playlist keeps the browser on screen;
            // the new listing arrives through the snapshot poll. Only a file
            // switches to the player.
            if (entry.kind != FILE_BROWSER_ENTRY_FILE) return;
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
            ui_set_state_line("Открытие файла", "", false);
            ui_scroller_set_text(&s_source_detail, file_browser_display_name(entry.name));
            ui_set_label_text_if_changed(s_source_stream,
                                         file_browser_entry_type_label(&entry));
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
            ui_set_state_line("Connecting", "", false);
            ui_scroller_set_text(&s_source_detail,
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
            audio_source_is_stations(source) || audio_source_is_files(source);
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
            ui_click_gesture_cancel(&s_like_click);
            ui_show_menu();
        } else if (has_list && action == BOARD_INPUT_ACTION_ENCODER_BUTTON) {
            // Double click opens the list, triple click starts scrubbing, and
            // both leave playback alone; the single click that toggles
            // play/pause is delivered later, from the poll loop, once no
            // further press has arrived.
            switch (ui_click_gesture_press(&s_player_click, ui_tick_get_ms(),
                                           ui_seek_available())) {
            case UI_CLICK_DOUBLE:
                ui_open_source_list(source);
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
            s_device_settings.volume = volume;
            s_volume_save_pending = true;
            s_volume_changed_ms = ui_tick_get_ms();
            /* Per detent, not per save: the web slider has to follow the knob
             * while it is being turned, and the save is a second and a half
             * behind on purpose. */
            device_settings_publish(&s_device_settings);
            ui_update_footer();
        } else if (action == BOARD_INPUT_ACTION_BTN_PREV ||
                   action == BOARD_INPUT_ACTION_BTN_NEXT) {
            const bool forward = action == BOARD_INPUT_ACTION_BTN_NEXT;
            player_command_t command = {
                .source = source,
                .item_index = PLAYER_ITEM_NONE,
            };
            if (source == AUDIO_SOURCE_YANDEX) {
                /* The rotor's chain runs one way only - there is no previous
                 * track to go back to - so on this source the two keys are not
                 * a pair: forward asks for the next track, and back carries
                 * both marks. A double press is the rejection, delivered at
                 * once; the single press waits out its window in the poll
                 * loop, which is the cost of the second gesture and is not
                 * felt on an action nothing is waiting for. */
                if (!forward) {
                    if (ui_click_gesture_press(&s_like_click, ui_tick_get_ms(), false) ==
                        UI_CLICK_DOUBLE) {
                        ui_submit_like_press(true);
                    }
                    return;
                }
                command.kind = PLAYER_COMMAND_NEXT_TRACK;
            } else if (has_list) {
                command.kind = forward ? PLAYER_COMMAND_NEXT_ITEM
                                       : PLAYER_COMMAND_PREVIOUS_ITEM;
            } else {
                return;
            }
            (void)ui_submit_player_command(&command);
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
            } else if (ui_network_source_blocked((ui_menu_item_t)item, s_feed_notice)) {
                return;
            } else if (item == UI_FEED_YANDEX) {
                /* Not a source yet - the account is linked here, and playback
                 * arrives in a later step. */
                ui_set_label_text_if_changed(s_feed_notice, "");
                ui_show_yandex();
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
        if (ui_network_source_blocked((ui_menu_item_t)ui_menu_selected_index(&s_menu),
                                      s_menu_notice)) {
            return;
        }
        if (ui_menu_selected_index(&s_menu) == (uint8_t)UI_MENU_ITEM_YANDEX_MUSIC) {
            ui_show_yandex();
            return;
        }
        ui_show_source();
    }
}

/* Keeps the resume point current. Written from the poll loop rather than at
 * the moment of selection because this is the one place that sees both the
 * active source and the file the USB player actually opened; the setters skip
 * a write when nothing changed, so this does not hammer the flash. */
/* Every marquee on the device, moved one frame. Driven from the poll loop
 * rather than from an LVGL animation because the offset is a function of the
 * clock: a frame missed while the decoder holds a core catches up instead of
 * leaving the line behind. Only the screen on show is walked - the rest are on
 * parents LVGL is not drawing. */
static void ui_scroll_tick(void)
{
    const ui_text_scroll_mode_t mode = s_device_settings.scroll == DEVICE_SCROLL_LEFT
                                           ? UI_TEXT_SCROLL_LEFT
                                           : UI_TEXT_SCROLL_BOUNCE;
    const uint32_t now = ui_tick_get_ms();
    lv_obj_t *active = lv_screen_active();
    if (active == s_source_screen) {
        ui_scroller_tick(&s_source_detail, mode, now);
    } else if (active == s_station_list_screen) {
        for (size_t row = 0; row < UI_STATION_LIST_MAX_ROWS; ++row) {
            ui_scroller_tick(&s_station_list_rows[row], mode, now);
        }
    } else if (active == s_yandex_screen) {
        for (size_t row = 0; row < UI_YANDEX_LIST_ROWS; ++row) {
            ui_scroller_tick(&s_yandex_rows[row], mode, now);
        }
    }
}

static void ui_remember_playing(const player_snapshot_t *snapshot)
{
    if (!s_device_settings.autoplay) return;
    switch (snapshot->active_source) {
    case AUDIO_SOURCE_INTERNET_RADIO:
        (void)device_settings_set_last_source(&s_device_settings,
                                              DEVICE_LAST_SOURCE_INTERNET_RADIO);
        return;
    case AUDIO_SOURCE_YANDEX: {
        /* The station by identity, asked of the controller for the reason the
         * file path is: the row on screen moves while a station plays on, and
         * the dashboard can hand the rows out in another order next time. */
        char id[DEVICE_LAST_YANDEX_ID_MAX];
        char name[DEVICE_LAST_YANDEX_NAME_MAX];
        char from[DEVICE_LAST_YANDEX_FROM_MAX];
        if (!player_control_playing_yandex_station(id, sizeof(id), name, sizeof(name), from,
                                                   sizeof(from))) {
            // Nothing has been started yet - leave the previous point alone.
            return;
        }
        (void)device_settings_set_last_source(&s_device_settings, DEVICE_LAST_SOURCE_YANDEX);
        (void)device_settings_set_last_yandex(&s_device_settings, id, name, from);
        return;
    }
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

/* The network went away for good while one of the two streaming sources was
 * playing. Waiting on a screen for a stream that cannot come back is worse than
 * being taken home, and going home also stops the source rather than leaving it
 * retrying behind a screen with nothing to say.
 *
 * Edge-triggered on the setup AP, not on "not connected": between losing a
 * network and giving up there is a retry that usually succeeds within seconds,
 * and throwing the user out of the player every time the air goes quiet would
 * be worse than the wait. */
static bool ui_network_lost_for_good(const player_snapshot_t *snapshot)
{
    const bool lost = snapshot->wifi_setup_ap && !s_last_wifi_setup_ap;
    s_last_wifi_setup_ap = snapshot->wifi_setup_ap;
    return lost && (snapshot->active_source == AUDIO_SOURCE_INTERNET_RADIO ||
                    snapshot->active_source == AUDIO_SOURCE_YANDEX);
}

static void ui_sync_player_snapshot(const player_snapshot_t *snapshot)
{
    if (snapshot == NULL) return;
    const ui_player_view_t old_view = ui_player_state_view(&s_player_ui);
    const audio_source_t old_source = ui_player_state_source(&s_player_ui);
    s_last_wifi_connected = snapshot->wifi_connected;
    if (ui_network_lost_for_good(snapshot)) {
        ui_show_menu();
        return;
    }
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
        ui_click_gesture_cancel(&s_like_click);
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
        if (ui_playback_running(snapshot) &&
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
        ui_autoplay_decide(&s_device_settings, snapshot->usb_media, snapshot->sd_media, false,
                           BOARD_HAS_YANDEX_MUSIC);
    const bool waited =
        (uint32_t)(ui_tick_get_ms() - s_autoplay_started_ms) >= UI_AUTOPLAY_WAIT_MS;
    // Hold off only while the answer could still change: a drive that has not
    // shown up yet may still mount.
    if (action == UI_AUTOPLAY_FILE_UNAVAILABLE && !waited) return;
    /* And a station cannot be selected before there is a network to reach it
     * over. player_control_decide() answers INVALID for a network source while
     * the Wi-Fi is down, which is what "invalid player command kind=0" in the
     * boot log was - kind 0 being SELECT_SOURCE, the first enumerator.
     *
     * That the radio started anyway was luck, not design: the two commands
     * queued below are gated on different fields - the select on the source it
     * names, the play on whatever source is already active - so the play only
     * got through *because* the select had failed and left the active source
     * at none. Half a second's difference in the DHCP lease and it would have
     * been the other way round. Waiting here settles it before either command
     * is written, and takes the failed DNS lookup and its retry with it. */
    const bool waited_for_network =
        (uint32_t)(ui_tick_get_ms() - s_autoplay_started_ms) >= UI_AUTOPLAY_NETWORK_WAIT_MS;
    if ((action == UI_AUTOPLAY_RADIO || action == UI_AUTOPLAY_YANDEX) &&
        !snapshot->wifi_connected && !waited_for_network) {
        return;
    }
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
    case UI_AUTOPLAY_YANDEX: {
        /* The same two commands the radio is resumed with, and in the same
         * order. What differs is the station: the radio keeps its own last URL
         * down in station_resume, while a Yandex station is three strings that
         * only settings.csv has at boot - so they are handed over before the
         * play, and the controller starts what it was given. */
        player_control_set_yandex_station(s_device_settings.last_yandex_id,
                                          s_device_settings.last_yandex_name,
                                          s_device_settings.last_yandex_from);
        (void)ui_menu_select_source(&s_menu, AUDIO_SOURCE_YANDEX);
        const player_command_t select = {
            .kind = PLAYER_COMMAND_SELECT_SOURCE,
            .source = AUDIO_SOURCE_YANDEX,
            .item_index = PLAYER_ITEM_NONE,
        };
        if (!ui_submit_player_command(&select)) return;
        const player_command_t play = {
            .kind = PLAYER_COMMAND_PLAY,
            .source = AUDIO_SOURCE_YANDEX,
            .item_index = PLAYER_ITEM_NONE,
        };
        (void)ui_submit_player_command(&play);
        ui_load_source_screen(AUDIO_SOURCE_YANDEX);
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
        if (ui_click_gesture_poll(&s_like_click, ui_tick_get_ms()) == UI_CLICK_SINGLE) {
            ui_submit_like_press(false);
        }
        switch (ui_click_gesture_poll(&s_player_click, ui_tick_get_ms())) {
        case UI_CLICK_SINGLE:
            ui_toggle_playback();
            break;
        // Only reachable where a third press was worth waiting for: with
        // scrubbing available the double has to outlast its own window before
        // it can be told from the start of a triple.
        case UI_CLICK_DOUBLE:
            ui_open_source_list(ui_player_state_source(&s_player_ui));
            break;
        default:
            break;
        }
        if (ui_volume_commit_due(s_volume_save_pending, s_volume_changed_ms,
                                 ui_tick_get_ms(), UI_VOLUME_SETTLE_MS)) {
            s_volume_save_pending = false;
            (void)device_settings_set_volume(&s_device_settings, board_audio_volume());
        }
        if (ui_volume_commit_due(s_brightness_save_pending, s_brightness_changed_ms,
                                 ui_tick_get_ms(), UI_BRIGHTNESS_SETTLE_MS)) {
            s_brightness_save_pending = false;
            (void)device_settings_set_brightness(&s_device_settings,
                                                 s_device_settings.brightness);
        }
        // After the two commits above, so a change of our own is already on the
        // card and cannot be read back as if it were somebody else's.
        if (device_settings_take_changed()) {
            ui_reload_settings();
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
                                       : active == s_yandex_screen       ? &s_yandex_strip
                                                                         : NULL;
            ui_status_strip_update(strip, &snapshot);
        }
        ui_update_vu();
        ui_update_footer();
        ui_update_cover();
        if (s_settings_open) {
            /* Wrap-safe, the same subtraction the station list's idle close
             * uses: the tick is a 32-bit millisecond counter. */
            if (s_qr_open &&
                (uint32_t)(ui_tick_get_ms() - s_qr_opened_ms) >= UI_QR_IDLE_TIMEOUT_MS) {
                ui_hide_qr();
            }
            ui_update_settings();
        } else if (s_yandex_open) {
            /* The player's own state still has to follow the snapshot: this
             * screen posts commands, and a command whose confirmation is never
             * read stays pending for ever - which is exactly what refused
             * every attempt to start a station from here. */
            ui_player_state_apply_snapshot(&s_player_ui, &snapshot, ui_tick_get_ms());
            ui_yandex_step_start(&snapshot);
            // Polled rather than driven by input: the countdown ticks and the
            // confirmation arrives from the network, neither of which is a
            // button press.
            ui_update_yandex();
            /* The same return timer the station list has, on the same 10 s.
             * Only over the list: a pairing code takes a minute to type on a
             * phone, and closing that screen out from under someone doing it
             * would be a fault, not a convenience. Nor while a station this
             * screen asked for is still starting - the wait is the screen
             * doing what it was told, not the user having wandered off. */
            if (s_yandex_open && s_yandex_mode == UI_YANDEX_MODE_LIST &&
                s_yandex_start_row == PLAYER_ITEM_NONE &&
                ui_playback_running(&snapshot) &&
                station_list_idle_timeout_elapsed(&s_yandex_list, ui_tick_get_ms(),
                                                  UI_STATION_LIST_IDLE_TIMEOUT_MS)) {
                ui_close_yandex();
            }
        } else {
            ui_sync_player_snapshot(&snapshot);
            /* After the sync, not before: it is the snapshot that clears the
             * pending stop this is waiting on. */
            if (s_radio_home_pending) s_radio_home_pending = !ui_open_radio_home();
        }
        ui_scroll_tick();
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
                         : active == s_yandex_screen  ? "yandex"
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
    /* The layout was compiled against the line heights copied into
     * ui_font_metrics.h from the generated fonts. A face regenerated at another
     * size, or a line mistyped when a size was added for a new panel, moves
     * every row on every screen by a pixel or two - which shows up as text that
     * touches and points at nothing. The fonts know their own metrics, but only
     * now that LVGL is up, so this is the earliest the copy can be checked. */
    if (UI_FONT_BODY->line_height != UI_FONT_BODY_LINE_H ||
        UI_FONT_TITLE->line_height != UI_FONT_TITLE_LINE_H) {
        ESP_LOGE(TAG,
                 "font metrics disagree with the layout: body %d vs %d, title %d vs %d",
                 (int)UI_FONT_BODY->line_height, (int)UI_FONT_BODY_LINE_H,
                 (int)UI_FONT_TITLE->line_height, (int)UI_FONT_TITLE_LINE_H);
    }

    s_input_queue = xQueueCreate(UI_INPUT_QUEUE_LENGTH, sizeof(board_input_action_t));
    if (s_input_queue == NULL) {
        ESP_LOGE(TAG, "input queue allocation failed");
        return ESP_ERR_NO_MEM;
    }
    ui_menu_init(&s_menu);
    ui_player_state_init(&s_player_ui);
    ui_click_gesture_init(&s_player_click);
    ui_click_gesture_init(&s_like_click);
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
    ui_create_yandex_screen();
    ui_create_source_screen();
    ui_create_station_list_screen();
    if (!device_settings_init(&s_device_settings)) {
        lv_label_set_text(s_settings_notice, "Ошибка чтения settings.csv");
    } else {
        ui_apply_display_rotation();
        (void)board_backlight_set(s_device_settings.brightness);
    }
    /* After the settings are read and the visibility they decide is applied:
     * the model asks how many rows the home screen would have, and before this
     * point the answer counts a Yandex row the switch may have turned off. */
    ui_apply_yandex_visibility();
    ui_settings_model_init(&s_settings_model, ui_home_screen_exists());
    // Before anything can play: the board defaults to full volume, and coming
    // back from a power cut at full blast when the user had it at 20 is the
    // kind of surprise a saved setting exists to prevent.
    board_audio_set_volume(s_device_settings.volume);
    // Before the task starts: a browser that connects first would otherwise be
    // told the device has no settings at all.
    device_settings_publish(&s_device_settings);
    // Asked with both volumes assumed ready: this only decides whether there is
    // anything to wait for at all. What is actually there is settled later, by
    // ui_autoplay_step(), once the drive has had time to enumerate.
    s_autoplay_pending = ui_autoplay_decide(&s_device_settings, FILE_BROWSER_MEDIA_READY,
                                            FILE_BROWSER_MEDIA_READY, true,
                                            BOARD_HAS_YANDEX_MUSIC) != UI_AUTOPLAY_HOME;
    s_autoplay_started_ms = ui_tick_get_ms();
    /* Through the same call the rest of the firmware uses, so a device with no
     * home screen boots straight into the radio instead of onto a screen it
     * would never show again. Autoplay, if it is on, replaces this a moment
     * later from the poll loop. */
    ui_load_menu_screen();

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
