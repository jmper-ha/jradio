#include "dlna_root_filter.h"

#include <stddef.h>
#include <string.h>

/* The names the sections actually carry. Plex uses the first three; MiniDLNA,
 * Twonky, Serviio and Windows Media Player between them account for the rest.
 *
 * The Cyrillic words appear twice because the comparison below folds case for
 * ASCII only. Folding UTF-8 properly would mean carrying a case table for one
 * decision that six literals settle, and getting it subtly wrong would hide a
 * section the user wanted. */
static const char *const s_not_music[] = {
    "video", "videos", "movie", "movies", "tv", "tv shows", "tv series",
    "recorded tv", "photo", "photos", "picture", "pictures", "image", "images",
    "Видео", "видео", "Фото", "фото", "Фотографии", "фотографии",
    "Изображения", "изображения", "Картинки", "картинки",
    "Фильмы", "фильмы", "Сериалы", "сериалы",
};

/* Long enough for every entry above with room to spare. A title longer than
 * this cannot match one, so it is music by default and never copied. */
#define DLNA_ROOT_FILTER_NAME_MAX 32U

static char ascii_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

bool dlna_root_filter_is_music(const char *title)
{
    if (title == NULL) return true;
    /* Leading and trailing spaces are the server's, not the user's: some name
     * their sections with an icon and a space in front. */
    while (*title == ' ') ++title;
    size_t length = strlen(title);
    while (length > 0U && title[length - 1U] == ' ') --length;
    if (length == 0U || length >= DLNA_ROOT_FILTER_NAME_MAX) return true;

    char folded[DLNA_ROOT_FILTER_NAME_MAX];
    for (size_t index = 0U; index < length; ++index) {
        folded[index] = ascii_lower(title[index]);
    }
    folded[length] = '\0';

    for (size_t index = 0U; index < sizeof(s_not_music) / sizeof(s_not_music[0]); ++index) {
        if (strcmp(folded, s_not_music[index]) == 0) return false;
    }
    return true;
}
