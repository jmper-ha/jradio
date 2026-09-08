#pragma once

#include <stdbool.h>

#include "audio_source.h"
#include "device_settings.h"
#include "file_browser.h"

/* What to do at boot when "autoplay" is on.
 *
 * The decision is separated out because it has more cases than it looks: the
 * drive may be missing, present but unreadable, or present with the remembered
 * file gone. Each of those wants a different screen, and none of them should
 * be discovered by reading the startup path in ui.c.
 */

typedef enum {
    /* Autoplay off, or nothing worth resuming - open the home screen. */
    UI_AUTOPLAY_HOME = 0,
    /* Resume the radio. The station itself comes from station_resume, which
     * has always kept the last URL. */
    UI_AUTOPLAY_RADIO,
    /* Resume the Yandex station named by the resume point. Unlike the radio
     * there is no fallback to "the first one": the dashboard has not been
     * fetched at boot, and waiting for it before any sound is most of what
     * autoplay is for - so with nothing remembered this is not returned. */
    UI_AUTOPLAY_YANDEX,
    /* Resume where the media server left off. Like Yandex and unlike the
     * radio, there is no "start at the top" fallback: a server's root is a
     * list of libraries with nothing playable in it, so with nothing
     * remembered this is not returned. */
    UI_AUTOPLAY_DLNA,
    /* The volume is there and so is the remembered file: play it. */
    UI_AUTOPLAY_FILE,
    /* The volume is there but the file is not - a different stick or card, or
     * it was deleted. Open the browser rather than an error: there is music
     * here, just not that track. */
    UI_AUTOPLAY_FILE_BROWSER,
    /* Nothing in the slot, or something that will not read. */
    UI_AUTOPLAY_FILE_UNAVAILABLE,
} ui_autoplay_action_t;

/* `usb_media` and `sd_media` describe the two volumes as the snapshot last saw
 * them; which one is consulted follows settings->last_source. `file_present`
 * says whether settings->last_file is still there; the caller answers that,
 * since it needs the filesystem, and it is only consulted when the volume is
 * usable.
 *
 * `yandex_built` and `dlna_built` are whether this firmware was built with
 * those sources at all - build options, which this layer cannot see. Whether
 * the user kept each row is settings->yandex_music and settings->dlna and is
 * checked here: a source taken off the home screen should not come back on its
 * own at the next power-on. */
ui_autoplay_action_t ui_autoplay_decide(const device_settings_t *settings,
                                        file_browser_media_t usb_media,
                                        file_browser_media_t sd_media,
                                        bool file_present, bool yandex_built,
                                        bool dlna_built);

// Which source the decision was about, so the caller can select it without
// reading last_source a second time and getting the mapping wrong.
audio_source_t ui_autoplay_source(const device_settings_t *settings);
