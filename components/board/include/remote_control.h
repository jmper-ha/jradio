#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "remote_map.h"

/* The remote control as the rest of the firmware sees it: keys arrive on the
 * input queue as actions, indistinguishable from the case's own buttons, and
 * the table that says which key is which is learned from the web page.
 *
 * Learning: arm a function, and the next key seen - a real key, not a repeat
 * frame - is bound to it and the table saved. While a function is armed no
 * key acts, or learning Power would put the board to sleep. The arming
 * expires on its own after REMOTE_LEARN_MS. */

/* Long enough to read the page, find the key and press it: ten seconds was
 * measured too short on the bench, with the key arriving after the arming
 * had quietly run out. */
#define REMOTE_LEARN_MS 30000U
#define REMOTE_MAP_PATH "/littlefs/config/remote.csv"

esp_err_t remote_control_init(void);

/* Waking by remote. The receiver's pin is a wake source, so any remote in the
 * room brings the chip out of deep sleep on the first mark of any frame; this
 * runs before board_init(), listens for a moment and says whether what came
 * was our Power key. The frame that woke the chip is lost to the boot itself
 * - a few hundred milliseconds - and NEC repeat frames carry no code, so the
 * key that wakes is the *second* press of Power, or a hold released and
 * pressed again. The listening lasts REMOTE_WAKE_LISTEN_MS, stretched while
 * a key is still being held. Idempotent with remote_control_init(), which
 * takes the receiver over afterwards. Never true on a board without the
 * receiver. */
#define REMOTE_WAKE_LISTEN_MS 3000U
/* First thing in the boot, before the settings are even read: starts the
 * receiver and keeps whatever it hears, so the listening covers as much of
 * the boot as it can. The check below judges what was kept once the table
 * is loaded. */
void remote_control_wake_listen(void);
bool remote_control_wake_check(void);

/* The web page's side. */
bool remote_control_learn(remote_function_t function);
bool remote_control_forget(remote_function_t function);
void remote_control_snapshot(remote_map_t *map, remote_function_t *learning);
/* Bumped on every change to the table or to what is armed, so a page can
 * tell "something happened" from one number in the settings frame. */
uint32_t remote_control_revision(void);

/* The last key the receiver saw - the function it meant, or
 * REMOTE_FUNCTION_COUNT for a key nobody has learned - and how long ago. The
 * page lights the row up, so a table can be checked key by key from the sofa
 * without the log. Not part of the revision: a held key sends ten frames a
 * second, and every browser on the settings page would get each one. */
typedef struct {
    remote_function_t function;
    ir_code_t code;
    uint32_t age_ms;
    bool seen;
} remote_last_key_t;
void remote_control_last_key(remote_last_key_t *last);
