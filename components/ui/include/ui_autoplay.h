#pragma once

#include <stdbool.h>

#include "device_settings.h"
#include "usb_browser.h"

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
    /* Drive is there and so is the remembered file: play it. */
    UI_AUTOPLAY_USB_FILE,
    /* Drive is there but the file is not - a different stick, or it was
     * deleted. Open the browser rather than an error: there is music here,
     * just not that track. */
    UI_AUTOPLAY_USB_BROWSER,
    /* No drive, or one that will not read. */
    UI_AUTOPLAY_USB_UNAVAILABLE,
} ui_autoplay_action_t;

/* `media` and `entry_count` describe the drive as the snapshot last saw it.
 * `file_present` says whether settings->last_usb_file is still on it; the
 * caller answers that, since it needs the filesystem. It is only consulted
 * when the drive is usable. */
ui_autoplay_action_t ui_autoplay_decide(const device_settings_t *settings,
                                        usb_browser_media_t media,
                                        bool file_present);
