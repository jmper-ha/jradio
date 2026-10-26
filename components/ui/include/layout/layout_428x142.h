#pragma once

/* Landscape 428x142: the NV3007 bar module. A third wider than the 320x170
 * module and 28 px shorter still - the shortest panel in the catalogue, and
 * the only one where the width is the room to spend. The faces are the
 * 320x170 module's, which are the smallest the tree carries.
 *
 * First cut, 2026-10-26, from the 320x170 numbers and the arithmetic in
 * ui_layout.h; nothing here has been looked at on the panel yet. */

/* The carousel: the tile one size down from the 320x170's, so that it goes
 * between the title (ending at 62) and the dots, which sit lower than the
 * derived TFT_HEIGHT - 40 to leave the tile its room. */
#define UI_FEED_INNER_DX 80
#define UI_FEED_OUTER_DX 150
#define UI_FEED_TILE 48
#define UI_FEED_ICON_LARGE_PX 44
#define UI_FEED_ICON_MEDIUM_PX 32
#define UI_FEED_ICON_SMALL_PX 24
#define UI_FEED_AXIS_Y 87
#define UI_FEED_DOT_Y 117
#define UI_FEED_NOTICE_Y 123

/* The list home screen: six items have to end on 142, so the rows are a
 * pixel shorter than the body face's line - the glyphs are 14 px and keep
 * their room; only the descenders' air goes. */
#define UI_MENU_FONT_PX 14
#define UI_MENU_ROW_PITCH 18
#define UI_MENU_ROW_H 18

#define UI_LIST_NOTICE_ICON_Y 34
#define UI_LIST_NOTICE_TEXT_Y 86

/* Lists: three rows of the title face, at four pixels of air instead of
 * twelve, from right under the strip. */
#define UI_LIST_ROW_Y (UI_STRIP_H + 2)
#define UI_LIST_ROW_PITCH (UI_FONT_TITLE_LINE_H + 4)
#define UI_LIST_ROW_H (UI_FONT_TITLE_LINE_H + 2)

/* Settings: three rows flush under the strip, and the web band at the body
 * face's glyph height rather than its line - thirty pixels of band would
 * leave two rows. */
#define UI_SET_ROW_Y (UI_STRIP_H + 2)
#define UI_SET_ROW_PITCH UI_FONT_TITLE_LINE_H
#define UI_SET_ROW_H UI_FONT_TITLE_LINE_H
#define UI_SET_BAND_H 18
#define UI_SET_BAND_Y (TFT_HEIGHT - UI_SET_BAND_H)
#define UI_SET_BAND_ADDRESS_W 220
#define UI_SET_BAND_SHOW_SCHEME true

/* Player screen: the 320x170 arrangement - the cover on the left with the
 * buffer or the time under it, everything else in a column beside it - with
 * the column's last line taken up into the room the width gives. The
 * readings (codec, bitrate) and the footer (like, volume) share the bottom
 * row, side by side, on the buffer's line; the meter and the position bar
 * go between the names and that row. */
#define UI_SRC_ART_SIZE 96
#define UI_SRC_ART_X 8
#define UI_SRC_ART_Y 27
#define UI_SRC_TEXT_X 112
#define UI_SRC_TEXT_W 306
#define UI_SRC_BODY_X 112
#define UI_SRC_ROW_TITLE 27
#define UI_SRC_ROW_TRACK 46
#define UI_SRC_ROW_ARTIST 69
#define UI_SRC_RULE_TOP 89
#define UI_SRC_VU_Y 91
#define UI_SRC_VU_BLOCK_H 6
#define UI_SRC_VU_PITCH 9

#define UI_SRC_FOOT_Y 123
#define UI_SRC_STREAM_X UI_SRC_TEXT_X
#define UI_SRC_STREAM_W 150
#define UI_SRC_STREAM_H UI_SRC_LINE_H
#define UI_SRC_STREAM_Y UI_SRC_FOOT_Y
#define UI_SRC_BUFFER_X UI_SRC_ART_X
#define UI_SRC_BUFFER_Y UI_SRC_FOOT_Y
#define UI_SRC_LIKE_X 272
#define UI_SRC_BT_ICON_X 296
#define UI_SRC_VOLUME_ICON_X 314
#define UI_SRC_VOLUME_BAR_X 334
#define UI_SRC_VOLUME_BAR_W 50
#define UI_SRC_VOLUME_TEXT_X 390

#define UI_SRC_STREAM_LINES 0
/* The readings bare - "MP3  128  44100" - to fit the 150 px beside the
 * footer; asked for on the panel 2026-10-26. */
#define UI_SRC_STREAM_BRIEF 1
#define UI_SRC_TEXT_CENTRED 0

/* FM: the frequency over the title and track rows, as on the 320x170
 * module; the radiotext on the performer's row, since the readings' row is
 * the footer's here; the reception marks under the picture. */
#define UI_SRC_FM_DIGITS_PX 40
#define UI_SRC_FM_TEXT_ROW 1
#define UI_SRC_FM_TEXT_X UI_SRC_TEXT_X
#define UI_SRC_FM_TEXT_Y UI_SRC_ROW_ARTIST
#define UI_SRC_FM_TEXT_W UI_SRC_TEXT_W
#define UI_SRC_FM_MARKS_IN_FOOTER 1
#define UI_SRC_FM_MARKS_X UI_SRC_BUFFER_X
#define UI_SRC_FM_MARKS_Y UI_SRC_BUFFER_Y
#define UI_SRC_FM_MARKS_W UI_SRC_ART_SIZE

#define UI_SRC_PAUSE_CENTRED 0
#define UI_SRC_PAUSE_X (UI_SRC_ART_X + (UI_SRC_ART_SIZE - UI_SRC_PAUSE_SIZE) / 2)
#define UI_SRC_PAUSE_Y (UI_SRC_ART_Y + (UI_SRC_ART_SIZE - UI_SRC_PAUSE_SIZE) / 2)

/* The QR card at the left with the caption beside it, as on the 320x170
 * module, a size smaller to fit 142. */
#define UI_QR_SIZE 104
#define UI_QR_CARD 134
#define UI_QR_CARD_X 4
#define UI_QR_CARD_Y 4
#define UI_QR_TEXT_BESIDE 1
#define UI_QR_CAPTION_X 150
#define UI_QR_CAPTION_W 268
#define UI_QR_CAPTION_Y 30
#define UI_QR_BACK_Y 110

/* The quick panel's four rows a little shorter, under the strip. */
#define UI_QUICK_ROW_H 23
#define UI_QUICK_SWITCH_H 18
#define UI_QUICK_PAD_Y 6
#define UI_QUICK_Y (UI_STRIP_H + 2)

/* About: the five rows closer than their line, which the 14 px glyphs
 * allow, and the two bottom lines on the edge. */
#define UI_ABOUT_TITLE_Y 2
#define UI_ABOUT_ROW_Y 24
#define UI_ABOUT_ROW_PITCH 16
#define UI_ABOUT_AUTHOR_Y 104
#define UI_ABOUT_HINT_Y 123

/* Yandex pairing: the 320x170 arrangement, squeezed. */
#define UI_YANDEX_STATUS_Y 28
#define UI_YANDEX_PANEL_Y 46
#define UI_YANDEX_CODE_Y 4
#define UI_YANDEX_URL_Y 42
#define UI_YANDEX_COUNTDOWN_Y 66
#define UI_YANDEX_PANEL_H 94

#define UI_FONT_BODY_PX 14
#define UI_STRIP_WEATHER_ICON_PX 16
#define UI_SAVER_CLOCK_PX 56
#define UI_SAVER_TEXT_PX 14
#define UI_SAVER_WEATHER_ICON_PX 16
#define UI_FONT_TITLE_PX 20
#define UI_FONT_ICON_PX 24
#define UI_FONT_DISPLAY_PX 32

#define UI_SRC_ART_NOTE_PX 64

/* The weather screensaver at the 320x170 module's sizes: the clock and the
 * weather on top and the readings under them, no forecast. */
#define UI_WX_BIG_PX 40
#define UI_WX_TEXT_PX 18
#define UI_WX_SMALL_PX 14
#define UI_WX_ICON_PX 48
#define UI_WX_DAY_ICON_PX 32
#define UI_WX_PICTO_PX 16
#define UI_WX_MARGIN 6
#define UI_WX_SHIFT_PX 3
#define UI_WX_FORECAST 0
#define UI_WX_RANGE_PX 14
#define UI_WX_PORTRAIT 0
#define UI_WX_READING_ROWS 0
#define UI_WX_DAYS_STACKED 0
#define UI_WX_SOURCE 0
#define UI_WX_READING_NAMES 0
#define UI_WX_WIND_POINT 0
#define UI_WX_GAP 6
#define UI_WX_RANGE_DEGREE 1
#define UI_WX_FEELS_BRIEF 1
