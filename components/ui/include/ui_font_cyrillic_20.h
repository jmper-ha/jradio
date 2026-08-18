#pragma once

#include "lvgl.h"

/* Title face for the player screen. Only the 14 px face existed before, and a
 * 20 px line of Cyrillic had nowhere to come from: LVGL ships Montserrat 24
 * and 48 in this build, but both are Latin only. */
extern const lv_font_t ui_font_cyrillic_20;
