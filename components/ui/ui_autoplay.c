#include "ui_autoplay.h"

#include <stddef.h>

ui_autoplay_action_t ui_autoplay_decide(const device_settings_t *settings,
                                        usb_browser_media_t media,
                                        bool file_present)
{
    if (settings == NULL || !settings->autoplay) return UI_AUTOPLAY_HOME;

    switch (settings->last_source) {
    case DEVICE_LAST_SOURCE_INTERNET_RADIO:
        /* No check that a station was ever saved: with none, the radio starts
         * at the top of the catalogue, which is still a player screen with
         * sound - closer to what autoplay promises than the home screen. */
        return UI_AUTOPLAY_RADIO;
    case DEVICE_LAST_SOURCE_USB:
        if (media != USB_BROWSER_MEDIA_READY) return UI_AUTOPLAY_USB_UNAVAILABLE;
        /* An empty remembered path means USB was the last source but nothing
         * had been played from it - the browser is where that left off. */
        if (settings->last_usb_file[0] == '\0') return UI_AUTOPLAY_USB_BROWSER;
        return file_present ? UI_AUTOPLAY_USB_FILE : UI_AUTOPLAY_USB_BROWSER;
    case DEVICE_LAST_SOURCE_NONE:
    default:
        return UI_AUTOPLAY_HOME;
    }
}
