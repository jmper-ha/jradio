#pragma once

#include "ui_feed_model.h"

/* LVGL's built-in symbols are part of the firmware font and avoid Unicode
 * emoji/font fallback problems on the small Cyrillic display. */
const char *ui_feed_icon_symbol(ui_feed_item_t item);
