#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "audio_source.h"
#include "icy_metadata.h"
#include "internet_radio_state.h"
#include "usb_browser.h"

#ifndef INTERNET_RADIO_TITLE_MAX_LEN
#define INTERNET_RADIO_TITLE_MAX_LEN ICY_METADATA_TITLE_MAX_LEN
#endif

#define PLAYER_NAME_MAX_LEN 64
#define PLAYER_TITLE_MAX_LEN INTERNET_RADIO_TITLE_MAX_LEN
#define PLAYER_CODEC_MAX_LEN 8
#define PLAYER_ERROR_MAX_LEN 64
#define PLAYER_ITEM_NONE SIZE_MAX
#define PLAYER_CAP_INTERNET_RADIO (1U << 0)
// Set only while a drive is mounted: selecting the USB source with nothing
// plugged in should be refused, not left showing an empty player screen.
#define PLAYER_CAP_USB (1U << 1)

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PLAYER_PLAYBACK_STOPPED,
    PLAYER_PLAYBACK_CONNECTING,
    PLAYER_PLAYBACK_PLAYING,
    PLAYER_PLAYBACK_PAUSED,
    PLAYER_PLAYBACK_RECONNECTING,
    PLAYER_PLAYBACK_ERROR,
} player_playback_state_t;

typedef enum {
    PLAYER_COMMAND_SELECT_SOURCE,
    PLAYER_COMMAND_STOP_SOURCE,
    PLAYER_COMMAND_PLAY,
    PLAYER_COMMAND_PAUSE,
    PLAYER_COMMAND_TOGGLE,
    PLAYER_COMMAND_SELECT_ITEM,
    // Posted by the USB player itself when a track ends on its own, never by
    // the UI or the web client. A stopped or failed track does not produce it,
    // so pressing stop cannot be mistaken for "play the next one".
    PLAYER_COMMAND_TRACK_FINISHED,
    // Leaves the directory being browsed. Only USB has a hierarchy; the radio
    // list is flat.
    PLAYER_COMMAND_BROWSE_UP,
    // Reopens the directory the playing file lives in, so that opening the
    // browser shows what is playing rather than wherever browsing last
    // stopped. Nothing starts or stops; only the listing moves.
    PLAYER_COMMAND_BROWSE_REVEAL,
    // Jumps the playing file to `position_seconds` and keeps playing from
    // there. Only a file can be seeked: a stream has no position to move to.
    PLAYER_COMMAND_SEEK,
} player_command_kind_t;

typedef struct {
    player_command_kind_t kind;
    audio_source_t source;
    size_t item_index;
    // Only PLAYER_COMMAND_SEEK reads this. Kept out of item_index rather than
    // borrowed from it: the two are different quantities, and a command that
    // lies about which one it carries is the kind of thing that survives a
    // rename.
    uint32_t position_seconds;
} player_command_t;

typedef struct {
    uint32_t capabilities;
    audio_source_t active_source;
    player_playback_state_t playback_state;
    size_t active_item_index;
    size_t item_count;
    /* Bumped every time the USB listing is refilled. Opening a directory can
     * leave both the count and the active index unchanged, so nothing else in
     * this struct distinguishes one directory from another - and the web needs
     * that to know when to re-fetch the listing over REST. */
    unsigned int usb_listing_revision;
    usb_browser_media_t usb_media;
    /* Separate from item_count, which describes whatever source is active: in
     * the menu that is the station catalog, so it says nothing about the
     * drive. This is what the "drive is empty" notice has to read. */
    size_t usb_entry_count;
    char context[PLAYER_NAME_MAX_LEN];
    char stream_title[PLAYER_TITLE_MAX_LEN];
    char codec[PLAYER_CODEC_MAX_LEN];
    uint16_t bitrate_kbps;
    uint32_t sample_rate_hz;
    bool wifi_rssi_valid;
    int8_t wifi_rssi_dbm;
    char error[PLAYER_ERROR_MAX_LEN];
} player_snapshot_t;

typedef enum {
    PLAYER_OPERATION_INVALID = -1,
    PLAYER_OPERATION_NONE,
    PLAYER_OPERATION_SELECT_SOURCE,
    PLAYER_OPERATION_STOP,
    PLAYER_OPERATION_START_SAVED,
    PLAYER_OPERATION_START_ITEM,
    PLAYER_OPERATION_PAUSE,
    PLAYER_OPERATION_RESUME,
    PLAYER_OPERATION_ADVANCE_ITEM,
    PLAYER_OPERATION_BROWSE_UP,
    PLAYER_OPERATION_BROWSE_REVEAL,
    PLAYER_OPERATION_SEEK,
} player_operation_t;

player_operation_t player_control_decide(const player_snapshot_t *state,
                                         const player_command_t *command);

#ifdef __cplusplus
}
#endif
