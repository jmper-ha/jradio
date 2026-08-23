#pragma once

#include "board_options.h"

/* board_options.h names the optional features; this turns "named or not" into
 * the plain 0/1 every user tests. Two spellings of "off" have to behave the
 * same - the line deleted, and the line left in place set to FEATURE_OFF -
 * which is exactly the kind of thing a bare #ifdef at each use site gets
 * wrong, one site at a time. */

#if defined(YANDEX_MUSIC) && YANDEX_MUSIC
#define BOARD_HAS_YANDEX_MUSIC 1
#else
#define BOARD_HAS_YANDEX_MUSIC 0
#endif
