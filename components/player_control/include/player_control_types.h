#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "audio_source.h"
#include "icy_metadata.h"
#include "internet_radio_state.h"
#include "file_browser.h"

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
/* The card slot, which is a different kind of statement: it is always set.
 *
 * A drive announces itself, so "mounted" is a fact worth gating on. Nothing
 * announces a card - no card-detect line is wired - so the only way to find
 * out is to mount it, and gating the source on the last attempt would make a
 * card inserted after boot unreachable: it would never be selectable, so it
 * would never be tried, so it would never be found. The source is therefore
 * always offered and the answer comes from the attempt, as sd_media in the
 * snapshot and as a line on the browser screen. */
#define PLAYER_CAP_SD (1U << 2)
/* Set only while an account is linked. Unlike the card slot there is nothing
 * to discover by trying: without a token every request fails the same way, so
 * offering the source would only produce an error screen. */
#define PLAYER_CAP_YANDEX (1U << 3)

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
    // Posted by the file player itself when a track ends on its own, never by
    // the UI or the web client. A stopped or failed track does not produce it,
    // so pressing stop cannot be mistaken for "play the next one".
    PLAYER_COMMAND_TRACK_FINISHED,
    // Leaves the directory being browsed. Only a filesystem has a hierarchy;
    // the radio list is flat.
    PLAYER_COMMAND_BROWSE_UP,
    // Reopens the directory the playing file lives in, so that opening the
    // browser shows what is playing rather than wherever browsing last
    // stopped. Nothing starts or stops; only the listing moves.
    PLAYER_COMMAND_BROWSE_REVEAL,
    // Jumps the playing file to `position_seconds` and keeps playing from
    // there. Only a file can be seeked: a stream has no position to move to.
    PLAYER_COMMAND_SEEK,
    /* Ends the playing track and moves on. Only a Yandex station has one:
     * a stream has no tracks, and a file list already advances by itself when
     * a track ends. Appended rather than slotted in - these values travel
     * through a queue, and renumbering them would misread a command already
     * posted across a firmware update. */
    PLAYER_COMMAND_NEXT_TRACK,
    /* The two track keys, for the sources that are a list: the station before
     * or after the one playing, the file before or after it in the directory.
     *
     * Nothing wraps. At the ends of the list the keys do nothing, which is the
     * behaviour asked for - a list that rolls over from the last station to
     * the first hides the fact that you have reached the end of it.
     *
     * Appended rather than slotted in beside SELECT_ITEM, for the reason
     * NEXT_TRACK was: these values travel through a queue. */
    PLAYER_COMMAND_PREVIOUS_ITEM,
    PLAYER_COMMAND_NEXT_ITEM,
    /* Adds the playing track to the account's liked tracks, or takes it back
     * out. Only the rotor has tracks that belong to an account. */
    PLAYER_COMMAND_TOGGLE_LIKE,
    /* The other list the same track can be in. Its own command rather than a
     * value on the one above, because the two surfaces that send it disagree
     * about what a press means: the web has a button per mark, and the device
     * has one key whose second press has to mean the other thing. */
    PLAYER_COMMAND_TOGGLE_DISLIKE,
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
    unsigned int listing_revision;
    /* One per volume rather than one for "the files source": the menu asks
     * about a source before selecting it, and for the card the answer has to
     * survive the card being unmounted between visits. */
    file_browser_media_t usb_media;
    file_browser_media_t sd_media;
    /* Separate from item_count, which describes whatever source is active: in
     * the menu that is the station catalog, so it says nothing about the
     * drive. This is what the "drive is empty" notice has to read. */
    size_t files_entry_count;
    char context[PLAYER_NAME_MAX_LEN];
    char stream_title[PLAYER_TITLE_MAX_LEN];
    char codec[PLAYER_CODEC_MAX_LEN];
    uint16_t bitrate_kbps;
    uint32_t sample_rate_hz;
    bool wifi_rssi_valid;
    int8_t wifi_rssi_dbm;
    /* Whether the playing track can be marked at all, and which mark it
     * carries. Separate from the marks themselves, because "no mark" and "no
     * such thing as a mark here" are different pictures: only a Yandex track
     * belongs to an account's library, so for a station or a file the heart is
     * absent rather than empty.
     *
     * The two marks exclude each other - that is how the API behaves and how
     * the rotor keeps them - so at most one of these is ever true. */
    bool track_likeable;
    bool track_liked;
    bool track_disliked;
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
    PLAYER_OPERATION_NEXT_TRACK,
    PLAYER_OPERATION_PREVIOUS_ITEM,
    PLAYER_OPERATION_NEXT_ITEM,
    PLAYER_OPERATION_TOGGLE_LIKE,
    PLAYER_OPERATION_TOGGLE_DISLIKE,
} player_operation_t;

player_operation_t player_control_decide(const player_snapshot_t *state,
                                         const player_command_t *command);

#ifdef __cplusplus
}
#endif
