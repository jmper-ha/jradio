#include "device_text.h"

#include <stddef.h>

typedef struct {
    const char *ru;
    const char *en;
} device_text_entry_t;

/* Keyed by the id rather than written in its order, so a lookup is an index -
 * this runs for every row of every list on every repaint - while the order of
 * the lines below means nothing. An entry written in the wrong place would
 * otherwise hand back its neighbour's words, which is the kind of fault that
 * survives a review. */
static const device_text_entry_t k_text[DEVICE_TEXT_COUNT] = {
    [DEVICE_TEXT_SOURCE_NONE] = {"Нет источника", "No source"},
    [DEVICE_TEXT_SOURCE_INTERNET_RADIO] = {"Интернет-радио", "Internet radio"},
    [DEVICE_TEXT_SOURCE_USB] = {"USB-плеер", "USB player"},
    [DEVICE_TEXT_SOURCE_SD] = {"SD-карта", "SD card"},
    [DEVICE_TEXT_SOURCE_YANDEX] = {"ЯМузыка", "Ya.Music"},
    /* Not translated, and not "Медиасервер": DLNA is what the protocol is
     * called in both languages, it is the word on the switch in Settings, and
     * it is what the user asked for. The sentences about a media server -
     * "Медиасервер не найден в сети" and the heading over a list of them - are
     * prose rather than the source's name, and stay as they are. */
    [DEVICE_TEXT_SOURCE_DLNA] = {"DLNA", "DLNA"},
    [DEVICE_TEXT_SOURCE_FM] = {"FM-радио", "FM radio"},
    [DEVICE_TEXT_SOURCE_BLUETOOTH] = {"Bluetooth", "Bluetooth"},
    [DEVICE_TEXT_SOURCE_UNKNOWN] = {"Неизвестный режим", "Unknown mode"},
    [DEVICE_TEXT_YANDEX_LONG] = {"Яндекс Музыка", "Yandex Music"},

    [DEVICE_TEXT_STATE_STOPPED] = {"Остановлено", "Stopped"},
    [DEVICE_TEXT_STATE_CONNECTING] = {"Подключение", "Connecting"},
    [DEVICE_TEXT_STATE_PLAYING] = {"Воспроизведение", "Playing"},
    [DEVICE_TEXT_STATE_PAUSED] = {"Пауза", "Paused"},
    [DEVICE_TEXT_STATE_RECONNECTING] = {"Переподключение", "Reconnecting"},
    [DEVICE_TEXT_STATE_ERROR] = {"Ошибка", "Error"},
    [DEVICE_TEXT_STATE_UNKNOWN] = {"Неизвестное состояние", "Unknown state"},

    [DEVICE_TEXT_SETTINGS] = {"Настройки", "Settings"},
    [DEVICE_TEXT_GROUP_LANGUAGE] = {"Язык", "Language"},
    [DEVICE_TEXT_GROUP_GENERAL] = {"Общие", "General"},
    [DEVICE_TEXT_GROUP_DISPLAY] = {"Экран", "Display"},
    [DEVICE_TEXT_ROW_LANGUAGE] = {"Язык", "Language"},
    /* Each language names itself in itself, the way every picker does it: a
     * reader who cannot read the current one still finds the other. */
    [DEVICE_TEXT_LANGUAGE_RUSSIAN] = {"Русский", "Русский"},
    [DEVICE_TEXT_LANGUAGE_ENGLISH] = {"English", "English"},
    [DEVICE_TEXT_ROW_HOME_SCREEN] = {"Главный экран", "Home screen"},
    [DEVICE_TEXT_HOME_SCREEN_LIST] = {"Список", "List"},
    [DEVICE_TEXT_HOME_SCREEN_FEED] = {"Лента", "Feed"},
    [DEVICE_TEXT_ROW_SCROLL] = {"Скроллинг", "Scrolling"},
    [DEVICE_TEXT_SCROLL_BOUNCE] = {"Влево-вправо", "Back and forth"},
    [DEVICE_TEXT_SCROLL_LEFT] = {"Влево", "Leftwards"},
    [DEVICE_TEXT_ROW_BUFFER_VIEW] = {"Буфер", "Buffer"},
    [DEVICE_TEXT_BUFFER_VIEW_TEXT] = {"Текст", "Text"},
    [DEVICE_TEXT_BUFFER_VIEW_GRAPH] = {"График", "Graph"},
    [DEVICE_TEXT_ROW_AUTOPLAY] = {"Автовоспроизведение", "Autoplay"},
    [DEVICE_TEXT_ROW_YANDEX] = {"Яндекс Музыка", "Yandex Music"},
    [DEVICE_TEXT_ROW_DLNA] = {"DLNA", "DLNA"},
    [DEVICE_TEXT_ROW_BRIGHTNESS] = {"Яркость", "Brightness"},
    [DEVICE_TEXT_ROW_SCREENSAVER] = {"Заставка", "Screensaver"},
    [DEVICE_TEXT_SCREENSAVER_OFF] = {"Нет", "None"},
    [DEVICE_TEXT_SCREENSAVER_DIM] = {"Затемнение", "Dim"},
    [DEVICE_TEXT_SCREENSAVER_BLANK] = {"Чёрный экран", "Blank"},
    [DEVICE_TEXT_SCREENSAVER_CLOCK] = {"Часы", "Clock"},
    [DEVICE_TEXT_ROW_SCREENSAVER_AFTER] = {"Через, с", "After, s"},
    [DEVICE_TEXT_ROW_IDLE_BRIGHTNESS] = {"Яркость в покое", "Idle brightness"},
    [DEVICE_TEXT_ROW_FLIP_VERTICAL] = {"Поворот по вертикали", "Flip vertically"},
    [DEVICE_TEXT_ROW_FLIP_HORIZONTAL] = {"Поворот по горизонтали", "Flip horizontally"},
    [DEVICE_TEXT_ROW_WEB_ADDRESS] = {"QR по нажатию", "QR on press"},
    [DEVICE_TEXT_ROW_ABOUT] = {"Об устройстве", "About"},
    [DEVICE_TEXT_ON] = {"вкл", "on"},
    [DEVICE_TEXT_OFF] = {"выкл", "off"},

    [DEVICE_TEXT_ABOUT_TITLE] = {"Об устройстве", "About"},
    [DEVICE_TEXT_ABOUT_FIRMWARE] = {"Прошивка", "Firmware"},
    [DEVICE_TEXT_ABOUT_BUILT] = {"Собрана", "Built"},
    [DEVICE_TEXT_ABOUT_WEB] = {"Веб-интерфейс", "Web UI"},
    [DEVICE_TEXT_ABOUT_UNKNOWN] = {"неизвестно", "unknown"},
    /* Short on purpose, and measured rather than guessed: Cyrillic is two
     * bytes a letter, and the sentence this replaced needed 85 of them against
     * a line that holds 63. It also has to fit the narrowest panel, where 29
     * letters is about what there is room for. */
    [DEVICE_TEXT_ABOUT_MISMATCH] = {"Веб-интерфейс от другой сборки",
     "Web UI is from another build"},
    [DEVICE_TEXT_ABOUT_IDF] = {"ESP-IDF", "ESP-IDF"},
    [DEVICE_TEXT_PRESS_TO_RETURN] = {"Нажмите, чтобы вернуться", "Press to return"},

    [DEVICE_TEXT_STATIONS] = {"Станции", "Stations"},
    [DEVICE_TEXT_BUFFER] = {"Буфер", "Buffer"},
    /* The percentage is the caller's to fill in. Kept as a format string so
     * that a language wanting the number elsewhere in the line can have it. */
    [DEVICE_TEXT_BUFFER_FORMAT] = {"Буфер %u%%", "Buffer %u%%"},
    [DEVICE_TEXT_BUFFER_UNKNOWN] = {"Буфер --", "Buffer --"},
    [DEVICE_TEXT_CHOOSE_TRACK] = {"Выберите трек", "Choose a track"},
    [DEVICE_TEXT_CHOOSE_FILE] = {"Выберите файл", "Choose a file"},
    [DEVICE_TEXT_OPENING_FILE] = {"Открытие файла", "Opening the file"},
    [DEVICE_TEXT_SEARCHING_SERVER] = {"Поиск медиасервера", "Looking for a media server"},
    [DEVICE_TEXT_NO_SERVER_FOUND] = {"Медиасервер не найден в сети",
     "No media server on the network"},
    /* The level above every server's own tree, which exists only when more
     * than one answered the search. */
    [DEVICE_TEXT_DLNA_SERVER_LIST] = {"Медиасерверы", "Media servers"},
    [DEVICE_TEXT_NO_NETWORK] = {"Нет сети — см. Настройки", "No network — see Settings"},
    /* Carries the network name, so it stays a format string. */
    [DEVICE_TEXT_JOIN_NETWORK] = {"Подключитесь к сети %s", "join %s"},
    [DEVICE_TEXT_WIFI_CONNECTING] = {"Подключение к сети...", "connecting to Wi-Fi..."},
    /* The band's one line for a box whose data partition was never written:
     * there is no Wi-Fi to report on, and "connecting" would be a lie. */
    [DEVICE_TEXT_DATA_NOT_FLASHED] = {"Прошейте раздел LittleFS", "flash the LittleFS partition"},
    [DEVICE_TEXT_NOT_AVAILABLE_YET] = {"Функция пока недоступна", "Not available yet"},
    [DEVICE_TEXT_SETTINGS_READ_FAILED] = {"Ошибка чтения settings.csv",
     "Cannot read settings.csv"},
    [DEVICE_TEXT_SETTINGS_WRITE_FAILED] = {"Ошибка записи settings.csv",
     "Cannot write settings.csv"},
    [DEVICE_TEXT_STATION_START_FAILED] = {"Не удалось запустить станцию",
     "The station would not start"},

    [DEVICE_TEXT_INSERT_USB] = {"Вставьте USB-флешку", "Insert a USB drive"},
    [DEVICE_TEXT_INSERT_CARD] = {"Вставьте карту памяти", "Insert a memory card"},
    [DEVICE_TEXT_USB_UNREADABLE] = {"Флешка не читается. Нужен формат FAT32",
     "The drive will not read. FAT32 is needed"},
    [DEVICE_TEXT_CARD_UNREADABLE] = {"Карта не читается. Нужен формат FAT32",
     "The card will not read. FAT32 is needed"},
    [DEVICE_TEXT_USB_EMPTY] = {"На флешке нет файлов", "The drive holds no files"},
    [DEVICE_TEXT_CARD_EMPTY] = {"На карте нет файлов", "The card holds no files"},

    /* These four are capped by PLAYER_ERROR_MAX_LEN (64 bytes). Cyrillic costs
     * two bytes a character there, so the Russian half is the tight one - see
     * the static assertion below, which is what caught the first attempt at
     * "Нечего включить - выберите в списке" at 65 bytes. */
    [DEVICE_TEXT_ERROR_STATION_FAILED] = {"Не удалось подключиться к станции",
     "Could not connect to the station"},
    [DEVICE_TEXT_ERROR_FILE_FAILED] = {"Не удалось воспроизвести файл",
     "Could not play the file"},
    [DEVICE_TEXT_ERROR_NOTHING_TO_RESUME] = {"Нечего продолжить - выберите",
     "Nothing to resume - choose something"},
    [DEVICE_TEXT_ERROR_FOLDER_FAILED] = {"Папка не открылась", "The folder would not open"},
    [DEVICE_TEXT_ERROR_YANDEX_SUBSCRIPTION] = {"Нужна подписка Яндекс Музыки",
     "A Yandex Music subscription is needed"},
    [DEVICE_TEXT_ERROR_YANDEX_REFRESH] = {"Обновить станции", "Refresh the stations"},
    [DEVICE_TEXT_ERROR_BAD_COMMAND] = {"Некорректная команда", "Malformed command"},
    [DEVICE_TEXT_ERROR_DEVICE_BUSY] = {"Устройство занято", "The device is busy"},

    [DEVICE_TEXT_YANDEX_NOT_LINKED] = {"Аккаунт не привязан", "Account not linked"},
    [DEVICE_TEXT_YANDEX_LINKED] = {"Аккаунт привязан", "Account linked"},
    [DEVICE_TEXT_YANDEX_ENTER_CODE] = {"Введите код на сайте", "Enter the code on the site"},
    [DEVICE_TEXT_YANDEX_REQUESTING] = {"Запрашиваем код...", "Requesting a code..."},
    [DEVICE_TEXT_YANDEX_SECONDS_LEFT] = {"Осталось %u с", "%u s left"},
    [DEVICE_TEXT_YANDEX_CODE_EXPIRED] = {"Код истёк", "The code expired"},
    [DEVICE_TEXT_YANDEX_DENIED] = {"Вход не подтверждён", "Sign-in was not confirmed"},
    [DEVICE_TEXT_YANDEX_NO_CONNECTION] = {"Нет связи с Яндексом", "No connection to Yandex"},
    [DEVICE_TEXT_YANDEX_SERVER_ERROR] = {"Ошибка сервера", "Server error"},
    [DEVICE_TEXT_YANDEX_SAVE_FAILED] = {"Не удалось сохранить", "Could not save"},
    [DEVICE_TEXT_YANDEX_LOADING] = {"Загрузка станций...", "Loading stations..."},
    [DEVICE_TEXT_YANDEX_LIST_FAILED] = {"Не удалось получить станции",
     "Could not fetch the stations"},
    [DEVICE_TEXT_YANDEX_LIST_EMPTY] = {"Станций нет", "No stations"},
    /* The gesture, not a button: F2 used to do this and no longer exists, so
     * the hints name the hold, which is what actually leaves a screen. */
    [DEVICE_TEXT_HINT_BACK] = {"удержание - назад", "hold - back"},
    [DEVICE_TEXT_HINT_CANCEL] = {"удержание - отмена", "hold - cancel"},
    [DEVICE_TEXT_HINT_LISTEN] = {"OK - слушать, удержание - назад", "OK - listen, hold - back"},
    [DEVICE_TEXT_HINT_RETRY] = {"OK - повторить, удержание - назад", "OK - retry, hold - back"},
    [DEVICE_TEXT_HINT_REFRESH] = {"OK - обновить, удержание - назад",
     "OK - refresh, hold - back"},
    [DEVICE_TEXT_HINT_LINK] = {"OK - привязать, удержание - назад", "OK - link, hold - back"},
};

/* An id added to the enum without a string here does not compile. That is the
 * whole point of the table being sized rather than sparse: a missing
 * translation is a build error, not a blank line on somebody's panel. */
_Static_assert(sizeof(k_text) / sizeof(k_text[0]) == (size_t)DEVICE_TEXT_COUNT,
               "the table is sized by the enum, so it cannot outgrow it");

const char *device_text(device_text_id_t id, device_language_t language)
{
    if ((size_t)id >= (size_t)DEVICE_TEXT_COUNT) return "?";
    const device_text_entry_t *const entry = &k_text[(size_t)id];
    const char *const text = language == DEVICE_LANGUAGE_EN ? entry->en : entry->ru;
    /* An id added to the enum and not to the table above lands here as a hole.
     * The host test walks every id and fails on it, so this is the belt to
     * that pair of braces - a question mark on a panel is at least visibly
     * wrong, where a null pointer is a crash inside LVGL. */
    return text != NULL ? text : "?";
}
