#include "ui_autoplay.h"

#include <stddef.h>

audio_source_t ui_autoplay_source(const device_settings_t *settings)
{
    if (settings == NULL) return AUDIO_SOURCE_NONE;
    switch (settings->last_source) {
    case DEVICE_LAST_SOURCE_INTERNET_RADIO: return AUDIO_SOURCE_INTERNET_RADIO;
    case DEVICE_LAST_SOURCE_USB:
    case DEVICE_LAST_SOURCE_SD:
        /* The path wins over the remembered source, because the two are
         * written at different moments and can disagree: leave a playing card
         * for the drive's browser without starting anything there, and the
         * source is saved as the drive while the path still names a file on
         * the card. Resuming that pair selected the drive and then played from
         * the card - one source active, the other one's directory on screen.
         * Whichever volume the file is on is the one that has to be opened. */
        if (settings->last_file[0] != '\0') {
            return file_browser_path_on_volume(settings->last_file, FILE_BROWSER_SD_ROOT)
                       ? AUDIO_SOURCE_SD
                       : AUDIO_SOURCE_USB;
        }
        return settings->last_source == DEVICE_LAST_SOURCE_SD ? AUDIO_SOURCE_SD
                                                              : AUDIO_SOURCE_USB;
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
        // Asked of the volume that will actually be opened - see
        // ui_autoplay_source(), which the remembered path can override.
        const file_browser_media_t media =
            ui_autoplay_source(settings) == AUDIO_SOURCE_SD ? sd_media : usb_media;
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
