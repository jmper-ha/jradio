#include "ui_feed_icons.h"

#include "lvgl.h"

const char *ui_feed_icon_symbol(ui_feed_item_t item)
{
    switch (item) {
    case UI_FEED_INTERNET_RADIO: return LV_SYMBOL_AUDIO;
    case UI_FEED_USB: return LV_SYMBOL_USB;
    case UI_FEED_BLUETOOTH: return LV_SYMBOL_BLUETOOTH;
    case UI_FEED_FM: return LV_SYMBOL_AUDIO;
    case UI_FEED_DLNA: return LV_SYMBOL_WIFI;
    case UI_FEED_YANDEX: return LV_SYMBOL_AUDIO;
    case UI_FEED_SETTINGS: return LV_SYMBOL_SETTINGS;
    default: return LV_SYMBOL_DUMMY;
    }
}
