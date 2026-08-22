#include "ui_usb_notice.h"

const char *ui_usb_notice(usb_browser_media_t media, size_t entry_count)
{
    switch (media) {
    case USB_BROWSER_MEDIA_ABSENT:
        return "Вставьте USB-флешку";
    case USB_BROWSER_MEDIA_UNREADABLE:
        // Names the two things the user can actually act on. The drive is
        // present, so "insert a drive" would be actively misleading.
        return "Флешка не читается. Нужен формат FAT32";
    case USB_BROWSER_MEDIA_READY:
        break;
    }
    // A mounted drive with nothing playable is not a fault, but an empty list
    // looks like one, so it is worth saying out loud.
    return entry_count == 0U ? "На флешке нет файлов" : NULL;
}

bool ui_usb_can_open(usb_browser_media_t media, size_t entry_count)
{
    return ui_usb_notice(media, entry_count) == NULL;
}

bool ui_usb_media_present(usb_browser_media_t media)
{
    return media == USB_BROWSER_MEDIA_READY;
}
