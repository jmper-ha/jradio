#pragma once

#include "device_settings.h"

/* Every word the device shows, in both languages, in one place.
 *
 * It lives beside the settings rather than in `ui` because the screen is not
 * the only face: the same source names and the same error lines go out to the
 * browser, some of them from `player_control` and `web_socket`, which have no
 * business depending on the panel. What they do all depend on is this
 * component, which is also where the chosen language is stored.
 *
 * The strings that are *not* here are as deliberate as the ones that are:
 *
 * - log messages, which are English by convention and are read by whoever is
 *   holding a serial cable, not by the user;
 * - the media server's section names in `dlna_root_filter.c`, which are
 *   patterns matched against what a server calls its libraries, not text shown
 *   to anybody - translating those would break the matching;
 * - anything the user typed or a server sent: station names, track titles,
 *   folder names.
 *
 * Adding an id without adding its two strings does not compile - see the
 * static assertion in device_text.c - and a host test walks every id in both
 * languages, so a half-translated build cannot ship. */

typedef enum {
    /* Sources, as the menu, the player block and the browser all name them. */
    DEVICE_TEXT_SOURCE_NONE = 0,
    DEVICE_TEXT_SOURCE_INTERNET_RADIO,
    DEVICE_TEXT_SOURCE_USB,
    DEVICE_TEXT_SOURCE_SD,
    DEVICE_TEXT_SOURCE_YANDEX,
    DEVICE_TEXT_SOURCE_DLNA,
    DEVICE_TEXT_SOURCE_FM,
    DEVICE_TEXT_SOURCE_BLUETOOTH,
    DEVICE_TEXT_SOURCE_UNKNOWN,
    /* The long name of the Yandex source, which the menu has room for and the
     * player block does not. */
    DEVICE_TEXT_YANDEX_LONG,

    /* What the player is doing. */
    DEVICE_TEXT_STATE_STOPPED,
    DEVICE_TEXT_STATE_CONNECTING,
    DEVICE_TEXT_STATE_PLAYING,
    DEVICE_TEXT_STATE_PAUSED,
    DEVICE_TEXT_STATE_RECONNECTING,
    DEVICE_TEXT_STATE_ERROR,
    DEVICE_TEXT_STATE_UNKNOWN,

    /* The settings screen: groups, rows and the values a row can take. */
    DEVICE_TEXT_SETTINGS,
    DEVICE_TEXT_GROUP_LANGUAGE,
    DEVICE_TEXT_GROUP_GENERAL,
    DEVICE_TEXT_GROUP_DISPLAY,
    DEVICE_TEXT_ROW_LANGUAGE,
    DEVICE_TEXT_LANGUAGE_RUSSIAN,
    DEVICE_TEXT_LANGUAGE_ENGLISH,
    DEVICE_TEXT_ROW_HOME_SCREEN,
    DEVICE_TEXT_HOME_SCREEN_LIST,
    DEVICE_TEXT_HOME_SCREEN_FEED,
    DEVICE_TEXT_ROW_SCROLL,
    DEVICE_TEXT_SCROLL_BOUNCE,
    DEVICE_TEXT_SCROLL_LEFT,
    DEVICE_TEXT_ROW_BUFFER_VIEW,
    DEVICE_TEXT_BUFFER_VIEW_TEXT,
    DEVICE_TEXT_BUFFER_VIEW_GRAPH,
    DEVICE_TEXT_ROW_AUTOPLAY,
    DEVICE_TEXT_ROW_WEATHER,
    DEVICE_TEXT_ROW_YANDEX,
    DEVICE_TEXT_ROW_DLNA,
    DEVICE_TEXT_ROW_BRIGHTNESS,
    DEVICE_TEXT_ROW_SCREENSAVER,
    DEVICE_TEXT_SCREENSAVER_OFF,
    DEVICE_TEXT_SCREENSAVER_DIM,
    DEVICE_TEXT_SCREENSAVER_BLANK,
    DEVICE_TEXT_SCREENSAVER_CLOCK,
    DEVICE_TEXT_ROW_FLIP_VERTICAL,
    DEVICE_TEXT_ROW_FLIP_HORIZONTAL,
    DEVICE_TEXT_ROW_WEB_ADDRESS,
    DEVICE_TEXT_ROW_ABOUT,
    DEVICE_TEXT_ON,
    DEVICE_TEXT_OFF,

    /* The About overlay. */
    DEVICE_TEXT_ABOUT_TITLE,
    DEVICE_TEXT_ABOUT_FIRMWARE,
    DEVICE_TEXT_ABOUT_BUILT,
    DEVICE_TEXT_ABOUT_WEB,
    DEVICE_TEXT_ABOUT_UNKNOWN,
    DEVICE_TEXT_ABOUT_MISMATCH,
    DEVICE_TEXT_ABOUT_IDF,
    DEVICE_TEXT_PRESS_TO_RETURN,

    /* The player block and the lists. */
    DEVICE_TEXT_STATIONS,
    DEVICE_TEXT_BUFFER,
    DEVICE_TEXT_BUFFER_FORMAT,
    DEVICE_TEXT_BUFFER_UNKNOWN,
    DEVICE_TEXT_CHOOSE_TRACK,
    DEVICE_TEXT_CHOOSE_FILE,
    DEVICE_TEXT_OPENING_FILE,
    DEVICE_TEXT_SEARCHING_SERVER,
    DEVICE_TEXT_NO_SERVER_FOUND,
    DEVICE_TEXT_DLNA_SERVER_LIST,
    DEVICE_TEXT_NO_NETWORK,
    DEVICE_TEXT_JOIN_NETWORK,
    DEVICE_TEXT_WIFI_CONNECTING,
    DEVICE_TEXT_DATA_NOT_FLASHED,
    DEVICE_TEXT_NOT_AVAILABLE_YET,
    DEVICE_TEXT_SETTINGS_READ_FAILED,
    DEVICE_TEXT_SETTINGS_WRITE_FAILED,
    DEVICE_TEXT_STATION_START_FAILED,

    /* What a drive or a card has to say for itself. */
    DEVICE_TEXT_INSERT_USB,
    DEVICE_TEXT_INSERT_CARD,
    DEVICE_TEXT_USB_UNREADABLE,
    DEVICE_TEXT_CARD_UNREADABLE,
    DEVICE_TEXT_USB_EMPTY,
    DEVICE_TEXT_CARD_EMPTY,

    /* Errors the controller puts in the snapshot, which both faces show. */
    DEVICE_TEXT_ERROR_STATION_FAILED,
    DEVICE_TEXT_ERROR_FILE_FAILED,
    DEVICE_TEXT_ERROR_NOTHING_TO_RESUME,
    DEVICE_TEXT_ERROR_FOLDER_FAILED,
    DEVICE_TEXT_ERROR_YANDEX_SUBSCRIPTION,
    DEVICE_TEXT_ERROR_YANDEX_REFRESH,
    DEVICE_TEXT_ERROR_BAD_COMMAND,
    DEVICE_TEXT_ERROR_DEVICE_BUSY,

    /* The Yandex Music screen: its status line and the hint under it. */
    DEVICE_TEXT_YANDEX_NOT_LINKED,
    DEVICE_TEXT_YANDEX_LINKED,
    DEVICE_TEXT_YANDEX_ENTER_CODE,
    DEVICE_TEXT_YANDEX_REQUESTING,
    DEVICE_TEXT_YANDEX_SECONDS_LEFT,
    DEVICE_TEXT_YANDEX_CODE_EXPIRED,
    DEVICE_TEXT_YANDEX_DENIED,
    DEVICE_TEXT_YANDEX_NO_CONNECTION,
    DEVICE_TEXT_YANDEX_SERVER_ERROR,
    DEVICE_TEXT_YANDEX_SAVE_FAILED,
    DEVICE_TEXT_YANDEX_LOADING,
    DEVICE_TEXT_YANDEX_LIST_FAILED,
    DEVICE_TEXT_YANDEX_LIST_EMPTY,
    DEVICE_TEXT_HINT_BACK,
    DEVICE_TEXT_HINT_CANCEL,
    DEVICE_TEXT_HINT_LISTEN,
    DEVICE_TEXT_HINT_RETRY,
    DEVICE_TEXT_HINT_REFRESH,
    DEVICE_TEXT_HINT_LINK,

    DEVICE_TEXT_COUNT,
} device_text_id_t;

/* Never NULL, and never empty: an id out of range answers with its own name in
 * neither language but something visible, because a blank on a panel reads as
 * a screen that failed rather than as a string nobody wrote. */
const char *device_text(device_text_id_t id, device_language_t language);
