#include "ui_files_notice.h"

const char *ui_files_notice(audio_source_t source, file_browser_media_t media,
                            size_t entry_count)
{
    const bool card = source == AUDIO_SOURCE_SD;
    switch (media) {
    case FILE_BROWSER_MEDIA_ABSENT:
        return card ? "Вставьте карту памяти" : "Вставьте USB-флешку";
    case FILE_BROWSER_MEDIA_UNREADABLE:
        // Names the two things the user can actually act on. The medium is
        // present, so "insert one" would be actively misleading.
        return card ? "Карта не читается. Нужен формат FAT32"
                    : "Флешка не читается. Нужен формат FAT32";
    case FILE_BROWSER_MEDIA_READY:
        break;
    }
    // A mounted volume with nothing playable is not a fault, but an empty list
    // looks like one, so it is worth saying out loud.
    if (entry_count != 0U) return NULL;
    return card ? "На карте нет файлов" : "На флешке нет файлов";
}

bool ui_files_can_open(audio_source_t source, file_browser_media_t media,
                       size_t entry_count)
{
    return ui_files_notice(source, media, entry_count) == NULL;
}

bool ui_files_media_present(file_browser_media_t media)
{
    return media == FILE_BROWSER_MEDIA_READY;
}
