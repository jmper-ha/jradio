#include "ui_feed_icons.h"

#include <stddef.h>

#include "ui_feed_icon_bitmaps.h"
#include "ui_layout.h"

/* Two levels so the size macro is expanded before it is pasted, the same shape
 * ui_fonts.h uses for its faces. The sizes are the layout's, so a panel that
 * wants a bigger ring says so in its shape file and the table follows; the
 * generator emits every size any of them asks for, and --gc-sections drops the
 * ones this build does not name. */
#define UI_FEED_ICON_AT_(name, px) ui_feed_icon_##name##_##px
#define UI_FEED_ICON_AT(name, px) UI_FEED_ICON_AT_(name, px)
#define UI_FEED_ICON_SET(name)                             \
    {&UI_FEED_ICON_AT(name, UI_FEED_ICON_SMALL_PX),        \
     &UI_FEED_ICON_AT(name, UI_FEED_ICON_MEDIUM_PX),       \
     &UI_FEED_ICON_AT(name, UI_FEED_ICON_LARGE_PX)}

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
