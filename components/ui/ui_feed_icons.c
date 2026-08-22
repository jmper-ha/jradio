#include "ui_feed_icons.h"

#include <stddef.h>

#include "ui_feed_icon_bitmaps.h"

#define UI_FEED_ICON_SET(name) \
    {&ui_feed_icon_##name##_24, &ui_feed_icon_##name##_32, &ui_feed_icon_##name##_48}

/* One row per feed item, three sizes each. Bluetooth, the SD card and settings
 * keep the meaning they always had and only change their drawing; the other
 * five are what the built-in symbols could not say at all. */
static const lv_image_dsc_t *const s_icons[UI_FEED_ITEM_COUNT][UI_FEED_ICON_SIZE_COUNT] = {
    [UI_FEED_INTERNET_RADIO] = UI_FEED_ICON_SET(podcasts),
    [UI_FEED_USB] = UI_FEED_ICON_SET(usb_stick),
    [UI_FEED_SD_CARD] = UI_FEED_ICON_SET(sd_card),
    [UI_FEED_BLUETOOTH] = UI_FEED_ICON_SET(bluetooth),
    [UI_FEED_FM] = UI_FEED_ICON_SET(radio),
    [UI_FEED_DLNA] = UI_FEED_ICON_SET(dlna),
    [UI_FEED_YANDEX] = UI_FEED_ICON_SET(spark),
    [UI_FEED_SETTINGS] = UI_FEED_ICON_SET(settings),
};

const lv_image_dsc_t *ui_feed_icon_image(ui_feed_item_t item, ui_feed_icon_size_t size)
{
    if (item >= UI_FEED_ITEM_COUNT || size >= UI_FEED_ICON_SIZE_COUNT) {
        return NULL;
    }
    return s_icons[item][size];
}
