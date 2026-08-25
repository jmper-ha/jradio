#pragma once

#include "board_options.h"

/* board_options.h says what this board has; this turns each answer into the
 * plain 0/1 every user tests. What the answers decide is the same everywhere:
 * whether the source appears on the home screen, whether the web interface
 * offers it, and whether the code that drives it runs at all.
 *
 * Two kinds of question, answered two ways.
 *
 * A part is present when its wiring is present - there is no second switch
 * saying "and it is really fitted", because a board with the pins named and
 * the part declared absent is a contradiction waiting to be believed. Delete
 * or comment out the block, and the source is gone.
 *
 * A feature has nothing to wire, so it is named directly, and two spellings of
 * "off" have to behave the same: the line deleted, and the line left in place
 * set to FEATURE_OFF. That is exactly the kind of thing a bare #ifdef at each
 * use site gets wrong, one site at a time. */

#if defined(USB_DP_GPIO)
#define BOARD_HAS_USB 1
#else
#define BOARD_HAS_USB 0
#endif

#if defined(SDC_CS_GPIO)
#define BOARD_HAS_SD_CARD 1
#else
#define BOARD_HAS_SD_CARD 0
#endif

#if defined(FM_TUNER) && FM_TUNER != FM_TUNER_NONE
#define BOARD_HAS_FM_RADIO 1
#else
#define BOARD_HAS_FM_RADIO 0
#endif

#if defined(BLUETOOTH) && BLUETOOTH != BLUETOOTH_NONE
#define BOARD_HAS_BLUETOOTH 1
#else
#define BOARD_HAS_BLUETOOTH 0
#endif

#if defined(YANDEX_MUSIC) && YANDEX_MUSIC
#define BOARD_HAS_YANDEX_MUSIC 1
#else
#define BOARD_HAS_YANDEX_MUSIC 0
#endif

#if defined(DLNA) && DLNA
#define BOARD_HAS_DLNA 1
#else
#define BOARD_HAS_DLNA 0
#endif
