#include "ui_autoplay.h"

#include <stddef.h>

audio_source_t ui_autoplay_source(const device_settings_t *settings)
{
    if (settings == NULL) return AUDIO_SOURCE_NONE;
    switch (settings->last_source) {
    case DEVICE_LAST_SOURCE_INTERNET_RADIO: return AUDIO_SOURCE_INTERNET_RADIO;
    case DEVICE_LAST_SOURCE_USB: return AUDIO_SOURCE_USB;
    case DEVICE_LAST_SOURCE_SD: return AUDIO_SOURCE_SD;
    case DEVICE_LAST_SOURCE_NONE:
    default: return AUDIO_SOURCE_NONE;
    }
}

ui_autoplay_action_t ui_autoplay_decide(const device_settings_t *settings,
                                        file_browser_media_t usb_media,
                                        file_browser_media_t sd_media,
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
    case DEVICE_LAST_SOURCE_SD: {
        const file_browser_media_t media =
            settings->last_source == DEVICE_LAST_SOURCE_SD ? sd_media : usb_media;
        if (media != FILE_BROWSER_MEDIA_READY) return UI_AUTOPLAY_FILE_UNAVAILABLE;
        /* An empty remembered path means the volume was the last source but
         * nothing had been played from it - the browser is where that left
         * off. */
        if (settings->last_file[0] == '\0') return UI_AUTOPLAY_FILE_BROWSER;
        return file_present ? UI_AUTOPLAY_FILE : UI_AUTOPLAY_FILE_BROWSER;
    }
    case DEVICE_LAST_SOURCE_NONE:
    default:
        return UI_AUTOPLAY_HOME;
    }
}
