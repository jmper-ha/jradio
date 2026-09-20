#include "remote_map.h"

#include <stdio.h>
#include <string.h>

/* The names are the file's and the page's; the order is the enum's. */
static const char *const k_names[REMOTE_FUNCTION_COUNT] = {
    [REMOTE_POWER] = "power",
    [REMOTE_VOLUME_UP] = "volume_up",
    [REMOTE_VOLUME_DOWN] = "volume_down",
    [REMOTE_MUTE] = "mute",
    [REMOTE_PLAY_PAUSE] = "play_pause",
    [REMOTE_PREV] = "prev",
    [REMOTE_NEXT] = "next",
    [REMOTE_UP] = "up",
    [REMOTE_DOWN] = "down",
    [REMOTE_OK] = "ok",
    [REMOTE_BACK] = "back",
    [REMOTE_MENU] = "menu",
    [REMOTE_QUICK] = "quick",
    [REMOTE_LIST] = "list",
    [REMOTE_SLEEP] = "sleep",
    [REMOTE_LIKE] = "like",
    [REMOTE_DISLIKE] = "dislike",
    [REMOTE_DIGIT_0] = "digit_0",
    [REMOTE_DIGIT_1] = "digit_1",
    [REMOTE_DIGIT_2] = "digit_2",
    [REMOTE_DIGIT_3] = "digit_3",
    [REMOTE_DIGIT_4] = "digit_4",
    [REMOTE_DIGIT_5] = "digit_5",
    [REMOTE_DIGIT_6] = "digit_6",
    [REMOTE_DIGIT_7] = "digit_7",
    [REMOTE_DIGIT_8] = "digit_8",
    [REMOTE_DIGIT_9] = "digit_9",
    [REMOTE_SOURCE_RADIO] = "source_radio",
    [REMOTE_SOURCE_USB] = "source_usb",
    [REMOTE_SOURCE_SD] = "source_sd",
    [REMOTE_SOURCE_BLUETOOTH] = "source_bluetooth",
    [REMOTE_SOURCE_YANDEX] = "source_yandex",
    [REMOTE_SOURCE_DLNA] = "source_dlna",
};

/* The 21-key NEC remote of the starter kits - "CAR MP3" on its face - with
 * address 0. Its keys, left to right and top to bottom: CH-, CH, CH+ / PREV,
 * NEXT, PLAY / -, +, EQ / 0, 100+, 200+ / 1..9. Read through the same NEC
 * decoder every other remote goes through, so a code here is exactly what
 * the log prints for that key. */
static const struct {
    remote_function_t function;
    uint8_t command;
} k_kit_remote[] = {
    {REMOTE_POWER, 0x45},       /* CH-, the top-left key: the nearest thing to a power key */
    {REMOTE_MENU, 0x46},        /* CH */
    {REMOTE_QUICK, 0x47},       /* CH+ */
    {REMOTE_PREV, 0x44},        /* |<< */
    {REMOTE_NEXT, 0x40},        /* >>| */
    {REMOTE_PLAY_PAUSE, 0x43},  /* >|| */
    {REMOTE_VOLUME_DOWN, 0x07}, /* - */
    {REMOTE_VOLUME_UP, 0x15},   /* + */
    {REMOTE_OK, 0x09},          /* EQ */
    {REMOTE_DIGIT_0, 0x16},
    {REMOTE_LIST, 0x19},        /* 100+ */
    {REMOTE_BACK, 0x0D},        /* 200+ */
    {REMOTE_DIGIT_1, 0x0C},
    {REMOTE_DIGIT_2, 0x18},
    {REMOTE_DIGIT_3, 0x5E},
    {REMOTE_DIGIT_4, 0x08},
    {REMOTE_DIGIT_5, 0x1C},
    {REMOTE_DIGIT_6, 0x5A},
    {REMOTE_DIGIT_7, 0x42},
    {REMOTE_DIGIT_8, 0x52},
    {REMOTE_DIGIT_9, 0x4A},
};

void remote_map_clear(remote_map_t *map)
{
    if (map == NULL) return;
    memset(map, 0, sizeof(*map));
}

void remote_map_init(remote_map_t *map)
{
    if (map == NULL) return;
    remote_map_clear(map);
    for (size_t index = 0U; index < sizeof(k_kit_remote) / sizeof(k_kit_remote[0]); ++index) {
        const ir_code_t code = {.kind = IR_CODE_NEC, .address = 0U,
                                .command = k_kit_remote[index].command, .hash = 0U};
        map->code[k_kit_remote[index].function] = code;
        map->bound[k_kit_remote[index].function] = true;
    }
}

const char *remote_function_name(remote_function_t function)
{
    if ((unsigned)function >= (unsigned)REMOTE_FUNCTION_COUNT) return NULL;
    return k_names[function];
}

bool remote_function_from_name(const char *name, remote_function_t *function)
{
    if (name == NULL || function == NULL) return false;
    for (unsigned index = 0U; index < (unsigned)REMOTE_FUNCTION_COUNT; ++index) {
        if (strcmp(name, k_names[index]) == 0) {
            *function = (remote_function_t)index;
            return true;
        }
    }
    return false;
}

bool remote_function_repeats(remote_function_t function)
{
    return function == REMOTE_VOLUME_UP || function == REMOTE_VOLUME_DOWN ||
           function == REMOTE_UP || function == REMOTE_DOWN;
}

board_input_action_t remote_function_action(remote_function_t function)
{
    switch (function) {
    case REMOTE_POWER: return BOARD_INPUT_ACTION_SLEEP_LONG;
    case REMOTE_VOLUME_UP: return BOARD_INPUT_ACTION_VOLUME_UP;
    case REMOTE_VOLUME_DOWN: return BOARD_INPUT_ACTION_VOLUME_DOWN;
    case REMOTE_MUTE: return BOARD_INPUT_ACTION_MUTE;
    case REMOTE_PLAY_PAUSE: return BOARD_INPUT_ACTION_PLAY_PAUSE;
    case REMOTE_PREV: return BOARD_INPUT_ACTION_BTN_PREV;
    case REMOTE_NEXT: return BOARD_INPUT_ACTION_BTN_NEXT;
    /* Up the list is the knob turned left - the direction the lists already
     * read the knob in. */
    case REMOTE_UP: return BOARD_INPUT_ACTION_ENCODER_LEFT;
    case REMOTE_DOWN: return BOARD_INPUT_ACTION_ENCODER_RIGHT;
    case REMOTE_OK: return BOARD_INPUT_ACTION_ENCODER_BUTTON;
    /* Both are the knob held: on every screen that is "back", and on the
     * player it is the home screen. One gesture, two keys that name it. */
    case REMOTE_BACK: return BOARD_INPUT_ACTION_ENCODER_LONG;
    case REMOTE_MENU: return BOARD_INPUT_ACTION_ENCODER_LONG;
    case REMOTE_QUICK: return BOARD_INPUT_ACTION_QUICK_MENU;
    case REMOTE_LIST: return BOARD_INPUT_ACTION_LIST;
    case REMOTE_SLEEP: return BOARD_INPUT_ACTION_SLEEP_CYCLE;
    case REMOTE_LIKE: return BOARD_INPUT_ACTION_LIKE;
    case REMOTE_DISLIKE: return BOARD_INPUT_ACTION_DISLIKE;
    case REMOTE_SOURCE_RADIO: return BOARD_INPUT_ACTION_SOURCE_RADIO;
    case REMOTE_SOURCE_USB: return BOARD_INPUT_ACTION_SOURCE_USB;
    case REMOTE_SOURCE_SD: return BOARD_INPUT_ACTION_SOURCE_SD;
    case REMOTE_SOURCE_BLUETOOTH: return BOARD_INPUT_ACTION_SOURCE_BLUETOOTH;
    case REMOTE_SOURCE_YANDEX: return BOARD_INPUT_ACTION_SOURCE_YANDEX;
    case REMOTE_SOURCE_DLNA: return BOARD_INPUT_ACTION_SOURCE_DLNA;
    default: break;
    }
    const int digit = remote_function_digit(function);
    if (digit >= 0) return (board_input_action_t)(BOARD_INPUT_ACTION_DIGIT_0 + digit);
    return BOARD_INPUT_ACTION_NONE;
}

int remote_function_digit(remote_function_t function)
{
    if (function >= REMOTE_DIGIT_0 && function <= REMOTE_DIGIT_9) {
        return (int)(function - REMOTE_DIGIT_0);
    }
    return -1;
}

remote_function_t remote_map_lookup(const remote_map_t *map, const ir_code_t *code)
{
    if (map == NULL || code == NULL) return REMOTE_FUNCTION_COUNT;
    for (unsigned index = 0U; index < (unsigned)REMOTE_FUNCTION_COUNT; ++index) {
        if (map->bound[index] && ir_code_same_key(&map->code[index], code)) {
            return (remote_function_t)index;
        }
    }
    return REMOTE_FUNCTION_COUNT;
}

remote_function_t remote_map_learn(remote_map_t *map, remote_function_t function,
                                   const ir_code_t *code)
{
    if (map == NULL || code == NULL || (unsigned)function >= (unsigned)REMOTE_FUNCTION_COUNT ||
        (code->kind != IR_CODE_NEC && code->kind != IR_CODE_RAW)) {
        return REMOTE_FUNCTION_COUNT;
    }
    remote_function_t displaced = remote_map_lookup(map, code);
    if (displaced == function) displaced = REMOTE_FUNCTION_COUNT;
    if (displaced != REMOTE_FUNCTION_COUNT) map->bound[displaced] = false;
    map->code[function] = *code;
    map->bound[function] = true;
    return displaced;
}

void remote_map_forget(remote_map_t *map, remote_function_t function)
{
    if (map == NULL || (unsigned)function >= (unsigned)REMOTE_FUNCTION_COUNT) return;
    map->bound[function] = false;
}

size_t remote_map_parse(remote_map_t *map, const char *text)
{
    if (map == NULL) return 0U;
    remote_map_init(map);
    if (text == NULL) return 0U;
    size_t skipped = 0U;
    const char *line = text;
    while (*line != '\0') {
        const char *end = strchr(line, '\n');
        const size_t length = end == NULL ? strlen(line) : (size_t)(end - line);
        char buffer[64];
        if (length > 0U && line[0] != '#') {
            if (length >= sizeof(buffer)) {
                ++skipped;
            } else {
                memcpy(buffer, line, length);
                buffer[length] = '\0';
                /* A trailing carriage return from a Windows editor is not
                 * part of the code. */
                if (length > 0U && buffer[length - 1U] == '\r') buffer[length - 1U] = '\0';
                char *comma = strchr(buffer, ',');
                remote_function_t function;
                if (comma == NULL) {
                    ++skipped;
                } else {
                    *comma = '\0';
                    const char *value = comma + 1;
                    ir_code_t code;
                    if (!remote_function_from_name(buffer, &function)) {
                        ++skipped;
                    } else if (strcmp(value, "none") == 0) {
                        remote_map_forget(map, function);
                    } else if (ir_code_parse(value, &code)) {
                        (void)remote_map_learn(map, function, &code);
                    } else {
                        ++skipped;
                    }
                }
            }
        }
        if (end == NULL) break;
        line = end + 1;
    }
    return skipped;
}

void remote_map_format(const remote_map_t *map, char *out, size_t size)
{
    if (out == NULL || size == 0U) return;
    out[0] = '\0';
    if (map == NULL) return;
    size_t used = (size_t)snprintf(out, size, "# function,code\n");
    for (unsigned index = 0U; index < (unsigned)REMOTE_FUNCTION_COUNT && used < size; ++index) {
        char code[24] = "none";
        if (map->bound[index]) ir_code_format(&map->code[index], code, sizeof(code));
        const int written = snprintf(out + used, size - used, "%s,%s\n", k_names[index], code);
        if (written < 0) break;
        used += (size_t)written;
    }
    if (used >= size) out[size - 1U] = '\0';
}
