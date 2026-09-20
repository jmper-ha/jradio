#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "board_input.h"
#include "ir_decode.h"

/* What a remote's keys mean: a function per key, and a key per function.
 *
 * The functions are the device's, not any remote's: "volume up", "the fourth
 * digit", "the Bluetooth source". Which key on which remote does each one is
 * a table the owner fills by learning - press "learn" beside a function,
 * press the key - and the table is remote.csv on the data partition. A
 * compiled-in table for the 21-key NEC remote that comes with every starter
 * kit fills the functions nobody has learned, so a fresh board answers to at
 * least one remote out of the box.
 *
 * One key means one function: learning a key that already meant something
 * else takes it away from that, because a table where Vol+ is also Next is a
 * table nobody can reason about. Pure, and tested on the host. */

typedef enum {
    REMOTE_POWER = 0,
    REMOTE_VOLUME_UP,
    REMOTE_VOLUME_DOWN,
    REMOTE_MUTE,
    REMOTE_PLAY_PAUSE,
    REMOTE_PREV,
    REMOTE_NEXT,
    REMOTE_UP,
    REMOTE_DOWN,
    REMOTE_OK,
    REMOTE_BACK,
    REMOTE_MENU,
    REMOTE_QUICK,
    REMOTE_LIST,
    REMOTE_SLEEP,
    REMOTE_LIKE,
    REMOTE_DISLIKE,
    REMOTE_DIGIT_0,
    REMOTE_DIGIT_1,
    REMOTE_DIGIT_2,
    REMOTE_DIGIT_3,
    REMOTE_DIGIT_4,
    REMOTE_DIGIT_5,
    REMOTE_DIGIT_6,
    REMOTE_DIGIT_7,
    REMOTE_DIGIT_8,
    REMOTE_DIGIT_9,
    REMOTE_SOURCE_RADIO,
    REMOTE_SOURCE_USB,
    REMOTE_SOURCE_SD,
    REMOTE_SOURCE_BLUETOOTH,
    REMOTE_SOURCE_YANDEX,
    REMOTE_SOURCE_DLNA,
    REMOTE_FUNCTION_COUNT,
} remote_function_t;

typedef struct {
    ir_code_t code[REMOTE_FUNCTION_COUNT];
    bool bound[REMOTE_FUNCTION_COUNT];
} remote_map_t;

/* The compiled-in table. */
void remote_map_init(remote_map_t *map);
/* Nothing bound at all - what "forget everything" leaves. */
void remote_map_clear(remote_map_t *map);

/* The file's names, "power", "digit_3", "source_usb" - what remote.csv and
 * the web page use. NULL for a value out of range. */
const char *remote_function_name(remote_function_t function);
bool remote_function_from_name(const char *name, remote_function_t *function);

/* Whether a held key keeps acting: the volume and the cursor ramp, everything
 * else fires once per press however long it is held - a held Next that
 * skipped three stations would be the remote's most-hated key. */
bool remote_function_repeats(remote_function_t function);

/* What the function is in the device's own vocabulary. Some are keys the
 * board already has - Back is the knob held - and some are new actions the
 * remote brought with it. */
board_input_action_t remote_function_action(remote_function_t function);

/* The digit a function stands for, 0-9, or -1. */
int remote_function_digit(remote_function_t function);

remote_function_t remote_map_lookup(const remote_map_t *map, const ir_code_t *code);
/* Binds the key to the function. Returns the function the key was taken
 * from, or REMOTE_FUNCTION_COUNT when it was free. */
remote_function_t remote_map_learn(remote_map_t *map, remote_function_t function,
                                   const ir_code_t *code);
void remote_map_forget(remote_map_t *map, remote_function_t function);

/* remote.csv in and out. The text form keeps only what differs from nothing:
 * every bound function as "name,code", an unbound one as "name,none" so a
 * forgotten default stays forgotten. Parsing starts from the compiled table
 * and overlays the file; a line it cannot read is skipped and counted. */
size_t remote_map_parse(remote_map_t *map, const char *text);
void remote_map_format(const remote_map_t *map, char *out, size_t size);
