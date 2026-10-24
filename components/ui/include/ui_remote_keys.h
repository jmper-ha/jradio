#pragma once

#include <stdbool.h>

#include "board_input.h"

/* What the remote's navigation keys mean on the screen in front of them.
 *
 * The case has a knob and two buttons whose meaning is already per screen -
 * the knob's turn is the volume on the player and the cursor in a list. The
 * remote has its own volume keys, so its Up, Down and OK are free to mean
 * what a television's do: on the player they open the list of what is
 * playing (Up and Down then move in it), and Prev and Next step along it as
 * they always did; in a menu or a list Up, Down, Prev and Next all move the
 * cursor and OK chooses. Play/Pause in a menu or a list starts what is
 * highlighted; anywhere else it is play and pause.
 *
 * Asked about on the forum on 2026-10-24: Up and Down were the volume on
 * the player, OK duplicated Play/Pause, Prev and Next did nothing in a menu,
 * and Play/Pause in the menu always started the radio. Pure, and tested on
 * the host; ui.c works out the screen and carries the answer out. */

typedef enum {
    // The player of the source playing, with nothing over it.
    UI_REMOTE_SCREEN_PLAYER = 0,
    // A home screen, a list of stations, files or waves: something to choose.
    UI_REMOTE_SCREEN_CHOOSER,
    // Anything else - the settings, the quick panel, a question on screen:
    // the keys are the knob and the case's buttons there, as they were.
    UI_REMOTE_SCREEN_OTHER,
} ui_remote_screen_t;

typedef struct {
    // What the rest of the input handling is to see; NONE for nothing more.
    board_input_action_t action;
    // Open the list of what is playing first (then `action` acts in it).
    bool open_list;
} ui_remote_step_t;

ui_remote_step_t ui_remote_keys_translate(board_input_action_t action, ui_remote_screen_t screen);
