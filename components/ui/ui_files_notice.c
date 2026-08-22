#include "ui_files_notice.h"

const char *ui_files_notice(file_browser_media_t media, size_t entry_count)
{
    switch (media) {
    case FILE_BROWSER_MEDIA_ABSENT:
        return "Вставьте USB-флешку";
    case FILE_BROWSER_MEDIA_UNREADABLE:
        // Names the two things the user can actually act on. The drive is
        // present, so "insert a drive" would be actively misleading.
        return "Флешка не читается. Нужен формат FAT32";
    case FILE_BROWSER_MEDIA_READY:
        break;
    }
    // A mounted drive with nothing playable is not a fault, but an empty list
    // looks like one, so it is worth saying out loud.
    return entry_count == 0U ? "На флешке нет файлов" : NULL;
}

bool ui_files_can_open(file_browser_media_t media, size_t entry_count)
{
    return ui_files_notice(media, entry_count) == NULL;
}

bool ui_files_media_present(file_browser_media_t media)
{
    return media == FILE_BROWSER_MEDIA_READY;
}
