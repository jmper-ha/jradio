#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "yandex_token_store.h"

static uint32_t s_counter;

static uint32_t counting_random(void)
{
    return s_counter++;
}

static void test_a_real_token_passes_and_junk_does_not(void)
{
    assert(yandex_token_is_valid("y0_AgAAAAABcDeFgHiJkLmNoPqRsTuVwXyZ"));
    assert(!yandex_token_is_valid(""));
    assert(!yandex_token_is_valid(NULL));
    /* A quote or a backslash would break out of the JSON string the token is
     * written into; a control character would corrupt a log line. */
    assert(!yandex_token_is_valid("has\"quote"));
    assert(!yandex_token_is_valid("has\\backslash"));
    assert(!yandex_token_is_valid("has space"));
    assert(!yandex_token_is_valid("has\nnewline"));
}

static void test_a_token_longer_than_the_field_is_rejected(void)
{
    /* Storing a truncated credential would produce a device that looks
     * authorised and fails every request. */
    char token[YANDEX_AUTH_TOKEN_MAX + 2];
    memset(token, 'a', sizeof(token) - 1U);
    token[sizeof(token) - 1U] = '\0';
    assert(!yandex_token_is_valid(token));
    token[YANDEX_AUTH_TOKEN_MAX] = '\0';
    assert(yandex_token_is_valid(token));
}

static void test_device_ids_are_alphanumeric_only(void)
{
    assert(yandex_token_device_id_is_valid("a1b2c3d4e5"));
    assert(!yandex_token_device_id_is_valid(""));
    assert(!yandex_token_device_id_is_valid(NULL));
    assert(!yandex_token_device_id_is_valid("has-dash"));
    assert(!yandex_token_device_id_is_valid("has_underscore"));
}

static void test_a_generated_id_fits_and_is_usable(void)
{
    char device_id[11];
    s_counter = 0U;
    yandex_token_generate_device_id(device_id, sizeof(device_id), counting_random);
    assert(strlen(device_id) == sizeof(device_id) - 1U);
    assert(yandex_token_device_id_is_valid(device_id));
    assert(strcmp(device_id, "abcdefghij") == 0);
}

static void test_generation_never_writes_past_the_buffer(void)
{
    /* The field is short on purpose; a caller that hands over a larger buffer
     * must still get an id the validator accepts. */
    char device_id[YANDEX_AUTH_DEVICE_ID_MAX + 64];
    memset(device_id, 'Z', sizeof(device_id));
    s_counter = 0U;
    yandex_token_generate_device_id(device_id, sizeof(device_id), counting_random);
    assert(strlen(device_id) == YANDEX_AUTH_DEVICE_ID_MAX);
    assert(yandex_token_device_id_is_valid(device_id));
}

static void test_bad_arguments_are_survivable(void)
{
    char device_id[4] = "xyz";
    yandex_token_generate_device_id(NULL, sizeof(device_id), counting_random);
    yandex_token_generate_device_id(device_id, 0U, counting_random);
    assert(strcmp(device_id, "xyz") == 0);
    /* No random source at all still produces something valid rather than an
     * empty id that later fails validation. */
    yandex_token_generate_device_id(device_id, sizeof(device_id), NULL);
    assert(yandex_token_device_id_is_valid(device_id));
}

int main(void)
{
    test_a_real_token_passes_and_junk_does_not();
    test_a_token_longer_than_the_field_is_rejected();
    test_device_ids_are_alphanumeric_only();
    test_a_generated_id_fits_and_is_usable();
    test_generation_never_writes_past_the_buffer();
    test_bad_arguments_are_survivable();
    printf("yandex_token_store tests passed\n");
    return 0;
}
