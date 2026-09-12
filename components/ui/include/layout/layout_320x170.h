#pragma once

/* Landscape 320x170: the 1.9" ST7789 module. As wide as the first panel and
 * 70 px shorter, which is a third of its height gone - every screen that was
 * placed by eye on 240 rows has to give something up here, and this file says
 * what. The faces are the first panel's: at 14 and 20 px they are already the
 * smallest the tree carries.
 *
 * Derived from the 320x240 numbers and the arithmetic in ui_layout.h; the
 * places marked "seen on the panel" are the ones that were looked at. */

/* The carousel, at the 320 px panel's spacing but a smaller selected tile:
 * the title under the strip ends at 62, the row of dots starts at 130, and 84
 * px of tile would not go between them. 60 leaves the icon its next size down
 * and two pixels of tile either side of it. */
#define UI_FEED_INNER_DX 68
#define UI_FEED_OUTER_DX 132
#define UI_FEED_TILE 60
#define UI_FEED_ICON_LARGE_PX 44
#define UI_FEED_ICON_MEDIUM_PX 32
#define UI_FEED_ICON_SMALL_PX 24

/* The axis the ring sits on: the tile's top edge two pixels under the title,
 * its bottom edge on the row of dots. */
#define UI_FEED_AXIS_Y 94

/* The list home screen in the body face rather than the title's: six items
 * at the title's 24 px pitch end at 174 on a 170 px panel, so the rows are
 * the body's 19 px line with three pixels of air, and six of them end at
 * 162. The 24 px icon overhangs a 22 px row by a pixel each side, which the
 * bitmap's own margin absorbs. A seventh item would run under the notice
 * line; no board in the catalogue offers seven. */
#define UI_MENU_FONT_PX 14
#define UI_MENU_ROW_PITCH 22
#define UI_MENU_ROW_H 22

/* The empty-browser screen: the drive under the strip, the sentence under
 * the drive, with the little air there is split between them. */
#define UI_LIST_NOTICE_ICON_Y 50
#define UI_LIST_NOTICE_TEXT_Y 104

/* The band along the bottom, the same as the panel of the same width. */
#define UI_SET_BAND_ADDRESS_W 190
#define UI_SET_BAND_SHOW_SCHEME true

/* Player screen: the cover on the left, and everything else - the names, the
 * readings, the meter, the position bar and the footer - in a column beside
 * it. Under the cover there is no room for any of them: the 320x240 panel
 * stacks the meter and the footer under a 96 px tile in 144 rows below the
 * strip, and 144 rows is all this panel has from the strip to its edge.
 *
 * So the column is packed: four lines of text flush against each other, the
 * meter at a smaller block on a tighter pitch, and the footer on the last
 * line with a pixel to spare. The cover keeps its 96 px and shares the
 * title's top edge, as on the wide panel; what is under it is the footer's
 * left slot - the buffer or the time - which is the one block of the footer
 * that fits a 96 px column, and taking it out of the footer beside the
 * cover is what gives the volume its bar back. */
#define UI_SRC_ART_SIZE 96
#define UI_SRC_ART_X 10
#define UI_SRC_ART_Y 27
/* Eight pixels past the tile, and the body of the screen starts where the
 * text does. */
#define UI_SRC_TEXT_X 114
#define UI_SRC_TEXT_W 196
#define UI_SRC_BODY_X 114
#define UI_SRC_ROW_TITLE 27
#define UI_SRC_ROW_TRACK 46
#define UI_SRC_ROW_ARTIST 69
/* One line across the column, under the performer. */
#define UI_SRC_STREAM_X UI_SRC_TEXT_X
#define UI_SRC_STREAM_W UI_SRC_TEXT_W
#define UI_SRC_STREAM_H UI_SRC_LINE_H
#define UI_SRC_STREAM_Y 88
#define UI_SRC_RULE_TOP 109
/* Two rows of 8 px blocks at a pitch of 12: the L and R marks are the body
 * face's capitals, ten pixels tall, and 12 is the pitch at which they do not
 * touch. */
#define UI_SRC_VU_Y 112
#define UI_SRC_VU_BLOCK_H 8
#define UI_SRC_VU_PITCH 12

/* The footer row: the buffer or the time under the cover, on the same line
 * as the controls beside it, and in the column the volume group where the
 * wide panel has it - the same bar, the number ending at 300 - with the like
 * mark centred in the room left between the column's edge and the icon. */
#define UI_SRC_FOOT_Y 150
#define UI_SRC_BUFFER_X UI_SRC_ART_X
#define UI_SRC_BUFFER_Y UI_SRC_FOOT_Y
#define UI_SRC_LIKE_X 144
#define UI_SRC_VOLUME_ICON_X 190
#define UI_SRC_VOLUME_BAR_X 208
#define UI_SRC_VOLUME_BAR_W 60
#define UI_SRC_VOLUME_TEXT_X 274

/* The readings run as one line and the names are read down their left edge,
 * as on the wide panel. */
#define UI_SRC_STREAM_LINES 0
#define UI_SRC_TEXT_CENTRED 0

/* The pause badge sits on the cover rather than in the middle of the screen,
 * where it would cover the names - the cover is the one block on its side
 * of the panel, and the badge is 76 px in a 96 px tile. */
#define UI_SRC_PAUSE_CENTRED 0
#define UI_SRC_PAUSE_X (UI_SRC_ART_X + (UI_SRC_ART_SIZE - UI_SRC_PAUSE_SIZE) / 2)
#define UI_SRC_PAUSE_Y (UI_SRC_ART_Y + (UI_SRC_ART_SIZE - UI_SRC_PAUSE_SIZE) / 2)

/* The QR card at the left with the caption and the way out beside it: a
 * card a phone reads needs about 160 px, and there are no 47 more under it
 * for two lines of text. 126 px of code is five pixels a module for the
 * address a device on a network shows; the setup network's longer payload
 * comes out at three, which a phone still reads at arm's length. */
#define UI_QR_SIZE 126
#define UI_QR_CARD 162
#define UI_QR_CARD_X 4
#define UI_QR_CARD_Y 4
#define UI_QR_TEXT_BESIDE 1
#define UI_QR_CAPTION_X 176
#define UI_QR_CAPTION_W 138
#define UI_QR_CAPTION_Y 40
#define UI_QR_BACK_Y 120

/* The About overlay with its air taken out: the title on the top edge, the
 * rows at the face's own line height, and the two bottom lines as close to
 * the edge as they go. Five rows end at 127, a pixel above the author. */
#define UI_ABOUT_TITLE_Y 4
#define UI_ABOUT_ROW_Y 32
#define UI_ABOUT_ROW_PITCH UI_SRC_LINE_H
#define UI_ABOUT_AUTHOR_Y 128
#define UI_ABOUT_HINT_Y 149

/* The Yandex pairing screen: the status line right under the strip and the
 * panel right under that, with the code in the 32 px display face rather
 * than the 48 px one. At 48 the panel is 122 px tall and there are 119 under
 * the status line. */
#define UI_YANDEX_STATUS_Y 30
#define UI_YANDEX_PANEL_Y 51
#define UI_YANDEX_CODE_Y 6
#define UI_YANDEX_URL_Y 50
#define UI_YANDEX_COUNTDOWN_Y 82
#define UI_YANDEX_PANEL_H 106

/* The faces this shape is laid out for: the 320x240 panel's, which are the
 * smallest there are - only the display face steps down, for the pairing
 * code above. */
#define UI_FONT_BODY_PX 14
#define UI_STRIP_WEATHER_ICON_PX 16
/* The screensaver at the portrait panel's sizes: a 56 px clock with the two
 * lines under it is about a hundred pixels, which leaves the block room to
 * be somewhere on 170. */
#define UI_SAVER_CLOCK_PX 56
#define UI_SAVER_TEXT_PX 14
#define UI_SAVER_WEATHER_ICON_PX 16
#define UI_FONT_TITLE_PX 20
#define UI_FONT_ICON_PX 24
#define UI_FONT_DISPLAY_PX 32

/* Two thirds of the 96 px tile, as on every panel with that tile. */
#define UI_SRC_ART_NOTE_PX 64
