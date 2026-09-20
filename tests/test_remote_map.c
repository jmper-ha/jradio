#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "remote_map.h"

static ir_code_t nec(uint16_t address, uint8_t command)
{
    return (ir_code_t){.kind = IR_CODE_NEC, .address = address, .command = command, .hash = 0U};
}

/* Field by field, not memcmp: the struct has padding, and padding is not
   part of the map. */
static bool maps_equal(const remote_map_t *left, const remote_map_t *right)
{
    for (unsigned index = 0U; index < (unsigned)REMOTE_FUNCTION_COUNT; ++index) {
        if (left->bound[index] != right->bound[index]) return false;
        if (left->bound[index] && !ir_code_same_key(&left->code[index], &right->code[index])) return false;
    }
    return true;
}

static void test_every_function_has_a_name_and_the_names_round_trip(void)
{
    for (unsigned index = 0U; index < (unsigned)REMOTE_FUNCTION_COUNT; ++index) {
        const char *name = remote_function_name((remote_function_t)index);
        assert(name != NULL && name[0] != '\0');
        remote_function_t back;
        assert(remote_function_from_name(name, &back) && back == (remote_function_t)index);
    }
    assert(remote_function_name(REMOTE_FUNCTION_COUNT) == NULL);
    remote_function_t function;
    assert(!remote_function_from_name("teleport", &function));
    assert(!remote_function_from_name(NULL, &function));
}

static void test_the_kit_remote_is_the_compiled_table(void)
{
    remote_map_t map;
    remote_map_init(&map);
    /* The kit's + key is the volume, its 1 is the digit, and its EQ is OK. */
    const ir_code_t plus = nec(0, 0x15);
    assert(remote_map_lookup(&map, &plus) == REMOTE_VOLUME_UP);
    const ir_code_t one = nec(0, 0x0C);
    assert(remote_map_lookup(&map, &one) == REMOTE_DIGIT_1);
    const ir_code_t eq = nec(0, 0x09);
    assert(remote_map_lookup(&map, &eq) == REMOTE_OK);
    /* The kit has no colour keys, so nothing is bound to Like. */
    assert(!map.bound[REMOTE_LIKE]);
    /* A key from another remote is nobody's. */
    const ir_code_t lg = nec(0x04, 0x08);
    assert(remote_map_lookup(&map, &lg) == REMOTE_FUNCTION_COUNT);
    /* And the repeat frame never is: it carries no key. */
    const ir_code_t repeat = {.kind = IR_CODE_NEC_REPEAT};
    assert(remote_map_lookup(&map, &repeat) == REMOTE_FUNCTION_COUNT);
}

static void test_learning_gives_a_key_one_function(void)
{
    remote_map_t map;
    remote_map_init(&map);
    /* The LG's Power replaces the kit's on the same function. */
    const ir_code_t lg_power = nec(0x04, 0x08);
    assert(remote_map_learn(&map, REMOTE_POWER, &lg_power) == REMOTE_FUNCTION_COUNT);
    assert(remote_map_lookup(&map, &lg_power) == REMOTE_POWER);
    const ir_code_t kit_power = nec(0, 0x45);
    assert(remote_map_lookup(&map, &kit_power) == REMOTE_FUNCTION_COUNT);

    /* The same key learned for a second function leaves the first. */
    assert(remote_map_learn(&map, REMOTE_MUTE, &lg_power) == REMOTE_POWER);
    assert(!map.bound[REMOTE_POWER]);
    assert(remote_map_lookup(&map, &lg_power) == REMOTE_MUTE);
    /* Learning the same key onto the function it already has displaces nothing. */
    assert(remote_map_learn(&map, REMOTE_MUTE, &lg_power) == REMOTE_FUNCTION_COUNT);

    /* A Dune key, extended NEC. */
    const ir_code_t dune_ok = nec(0xBF00, 0x14);
    assert(remote_map_learn(&map, REMOTE_OK, &dune_ok) == REMOTE_FUNCTION_COUNT);
    assert(remote_map_lookup(&map, &dune_ok) == REMOTE_OK);
    /* And a raw one. */
    const ir_code_t raw = {.kind = IR_CODE_RAW, .hash = 0x9f3e21c7U};
    assert(remote_map_learn(&map, REMOTE_LIST, &raw) == REMOTE_FUNCTION_COUNT);
    assert(remote_map_lookup(&map, &raw) == REMOTE_LIST);

    /* Not a key: refused. */
    const ir_code_t repeat = {.kind = IR_CODE_NEC_REPEAT};
    assert(remote_map_learn(&map, REMOTE_NEXT, &repeat) == REMOTE_FUNCTION_COUNT);
    assert(remote_map_lookup(&map, &(ir_code_t){.kind = IR_CODE_NEC, .address = 0, .command = 0x40}) == REMOTE_NEXT);

    remote_map_forget(&map, REMOTE_OK);
    assert(remote_map_lookup(&map, &dune_ok) == REMOTE_FUNCTION_COUNT);
}

static void test_the_file_round_trips_and_a_forgotten_default_stays_forgotten(void)
{
    remote_map_t map;
    remote_map_init(&map);
    const ir_code_t lg_power = nec(0x04, 0x08);
    (void)remote_map_learn(&map, REMOTE_POWER, &lg_power);
    remote_map_forget(&map, REMOTE_DIGIT_9);
    const ir_code_t raw = {.kind = IR_CODE_RAW, .hash = 0x12345678U};
    (void)remote_map_learn(&map, REMOTE_LIKE, &raw);

    char text[2048];
    remote_map_format(&map, text, sizeof(text));
    assert(strstr(text, "power,nec:4:08\n") != NULL);
    assert(strstr(text, "digit_9,none\n") != NULL);
    assert(strstr(text, "like,raw:12345678\n") != NULL);
    assert(strstr(text, "volume_up,nec:0:15\n") != NULL);

    remote_map_t back;
    assert(remote_map_parse(&back, text) == 0U);
    assert(maps_equal(&back, &map));
}

static void test_a_file_overlays_the_compiled_table_and_skips_what_it_cannot_read(void)
{
    remote_map_t map;
    const size_t skipped = remote_map_parse(&map,
        "# function,code\r\n"
        "power,nec:4:08\r\n"
        "teleport,nec:4:09\n"      /* no such function */
        "next,nec:4\n"             /* not a code */
        "mute,none\n"
        "this line has no comma\n"
        "digit_1,raw:00c0ffee\n");
    assert(skipped == 3U);
    const ir_code_t lg_power = nec(0x04, 0x08);
    assert(remote_map_lookup(&map, &lg_power) == REMOTE_POWER);
    /* The kit's keys the file did not mention are still there. */
    const ir_code_t plus = nec(0, 0x15);
    assert(remote_map_lookup(&map, &plus) == REMOTE_VOLUME_UP);
    assert(!map.bound[REMOTE_MUTE]);
    assert(map.bound[REMOTE_DIGIT_1] && map.code[REMOTE_DIGIT_1].kind == IR_CODE_RAW);
    /* The kit's 1 was displaced by the raw key on the same function. */
    const ir_code_t one = nec(0, 0x0C);
    assert(remote_map_lookup(&map, &one) == REMOTE_FUNCTION_COUNT);

    /* An empty or missing file is the compiled table. */
    remote_map_t fresh;
    assert(remote_map_parse(&fresh, "") == 0U);
    remote_map_t compiled;
    remote_map_init(&compiled);
    assert(maps_equal(&fresh, &compiled));
    assert(remote_map_parse(&fresh, NULL) == 0U);
}

static void test_what_each_function_does_on_the_board(void)
{
    assert(remote_function_action(REMOTE_POWER) == BOARD_INPUT_ACTION_SLEEP_LONG);
    assert(remote_function_action(REMOTE_UP) == BOARD_INPUT_ACTION_ENCODER_LEFT);
    assert(remote_function_action(REMOTE_DOWN) == BOARD_INPUT_ACTION_ENCODER_RIGHT);
    assert(remote_function_action(REMOTE_OK) == BOARD_INPUT_ACTION_ENCODER_BUTTON);
    assert(remote_function_action(REMOTE_BACK) == BOARD_INPUT_ACTION_ENCODER_LONG);
    assert(remote_function_action(REMOTE_QUICK) == BOARD_INPUT_ACTION_QUICK_MENU);
    assert(remote_function_action(REMOTE_DIGIT_0) == BOARD_INPUT_ACTION_DIGIT_0);
    assert(remote_function_action(REMOTE_DIGIT_7) == BOARD_INPUT_ACTION_DIGIT_7);
    assert(remote_function_action(REMOTE_SOURCE_DLNA) == BOARD_INPUT_ACTION_SOURCE_DLNA);
    assert(remote_function_action(REMOTE_FUNCTION_COUNT) == BOARD_INPUT_ACTION_NONE);
    /* Every function is some action: a key with nothing behind it is a bug
       the table would hide. */
    for (unsigned index = 0U; index < (unsigned)REMOTE_FUNCTION_COUNT; ++index) {
        assert(remote_function_action((remote_function_t)index) != BOARD_INPUT_ACTION_NONE);
    }
    assert(remote_function_digit(REMOTE_DIGIT_4) == 4);
    assert(remote_function_digit(REMOTE_OK) == -1);
    /* Held keys: the volume and the cursor ramp, nothing else does. */
    assert(remote_function_repeats(REMOTE_VOLUME_UP));
    assert(remote_function_repeats(REMOTE_DOWN));
    assert(!remote_function_repeats(REMOTE_NEXT));
    assert(!remote_function_repeats(REMOTE_DIGIT_1));
    assert(!remote_function_repeats(REMOTE_POWER));
}

int main(void)
{
    test_every_function_has_a_name_and_the_names_round_trip();
    test_the_kit_remote_is_the_compiled_table();
    test_learning_gives_a_key_one_function();
    test_the_file_round_trips_and_a_forgotten_default_stays_forgotten();
    test_a_file_overlays_the_compiled_table_and_skips_what_it_cannot_read();
    test_what_each_function_does_on_the_board();
    printf("remote_map tests passed\n");
    return 0;
}
