#include "ui_files_notice.h"

const char *ui_files_notice(audio_source_t source, file_browser_media_t media,
                            size_t entry_count, device_language_t language)
{
    const bool card = source == AUDIO_SOURCE_SD;
    switch (media) {
    case FILE_BROWSER_MEDIA_ABSENT:
        return device_text(card ? DEVICE_TEXT_INSERT_CARD : DEVICE_TEXT_INSERT_USB, language);
    case FILE_BROWSER_MEDIA_UNREADABLE:
        // Names the two things the user can actually act on. The medium is
        // present, so "insert one" would be actively misleading.
        return device_text(card ? DEVICE_TEXT_CARD_UNREADABLE : DEVICE_TEXT_USB_UNREADABLE,
                           language);
    case FILE_BROWSER_MEDIA_READY:
        break;
    }
    // A mounted volume with nothing playable is not a fault, but an empty list
    // looks like one, so it is worth saying out loud.
    if (entry_count != 0U) return NULL;
    return device_text(card ? DEVICE_TEXT_CARD_EMPTY : DEVICE_TEXT_USB_EMPTY, language);
}

bool ui_files_can_open(audio_source_t source, file_browser_media_t media,
                       size_t entry_count)
{
    /* Any language will do for a yes/no: the question is whether there is a
     * notice at all, and the table has an entry in both or in neither. */
    return ui_files_notice(source, media, entry_count, DEVICE_LANGUAGE_RU) == NULL;
}

bool ui_files_media_present(file_browser_media_t media)
{
    return media == FILE_BROWSER_MEDIA_READY;
}
