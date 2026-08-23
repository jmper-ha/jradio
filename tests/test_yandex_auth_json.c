#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "yandex_auth.h"

/* Captured from the real server on 2026-08-23 rather than invented, so a
 * change in the answer shape shows up here first. */
static const char *const REAL_DEVICE_CODE =
    "{\"device_code\":\"4b0a1c2d3e4f5a6b7c8d9e0f\",\"user_code\":\"gm2anfv7\","
    "\"verification_url\":\"https://ya.ru/device\",\"interval\":5,\"expires_in\":300}";

static void test_the_real_answer_yields_everything_the_screen_needs(void)
{
    yandex_auth_device_code_t code;
    assert(yandex_auth_parse_device_code(REAL_DEVICE_CODE, &code));
    assert(strcmp(code.user_code, "gm2anfv7") == 0);
    assert(strcmp(code.verification_url, "https://ya.ru/device") == 0);
    assert(strcmp(code.device_code, "4b0a1c2d3e4f5a6b7c8d9e0f") == 0);
    assert(code.interval_s == 5U);
    assert(code.expires_in_s == 300U);
}

static void test_a_missing_field_fails_the_whole_answer(void)
{
    /* Half a device code is not something to show and poll with: without the
     * verification URL the user has a code and nowhere to type it. */
    yandex_auth_device_code_t code;
    assert(!yandex_auth_parse_device_code(
        "{\"device_code\":\"abc\",\"user_code\":\"xyz\"}", &code));
    assert(code.user_code[0] == '\0');
    assert(!yandex_auth_parse_device_code("{\"user_code\":\"xyz\","
                                          "\"verification_url\":\"https://ya.ru/device\"}",
                                          &code));
    assert(!yandex_auth_parse_device_code("not json at all", &code));
    assert(!yandex_auth_parse_device_code(NULL, &code));
    assert(!yandex_auth_parse_device_code(REAL_DEVICE_CODE, NULL));
}

static void test_missing_timings_fall_back_rather_than_fail(void)
{
    /* The clamp helpers substitute defaults for zero, so an answer without an
     * interval is still usable - unlike a missing code. */
    yandex_auth_device_code_t code;
    assert(yandex_auth_parse_device_code(
        "{\"device_code\":\"abc\",\"user_code\":\"xyz\","
        "\"verification_url\":\"https://ya.ru/device\"}",
        &code));
    assert(code.interval_s == 0U);
    assert(code.expires_in_s == 0U);
    assert(yandex_auth_flow_clamp_interval(code.interval_s) == 5000U);
}

static void test_an_overlong_field_is_refused_not_truncated(void)
{
    /* A clipped device_code would poll forever against a code the server has
     * never heard of, which reads as "Yandex is broken". */
    char json[YANDEX_AUTH_DEVICE_CODE_MAX + 128];
    char long_code[YANDEX_AUTH_DEVICE_CODE_MAX + 2];
    memset(long_code, 'a', sizeof(long_code) - 1U);
    long_code[sizeof(long_code) - 1U] = '\0';
    snprintf(json, sizeof(json),
             "{\"device_code\":\"%s\",\"user_code\":\"xyz\","
             "\"verification_url\":\"https://ya.ru/device\"}",
             long_code);
    yandex_auth_device_code_t code;
    assert(!yandex_auth_parse_device_code(json, &code));
}

static void test_the_confirmed_answer_hands_over_the_token(void)
{
    char token[YANDEX_AUTH_TOKEN_MAX + 1];
    assert(yandex_auth_parse_token(
               "{\"access_token\":\"y0_AgAAAAA\",\"expires_in\":31536000,"
               "\"refresh_token\":\"1:abc\",\"token_type\":\"bearer\"}",
               token, sizeof(token)) == YANDEX_AUTH_TOKEN_OK);
    assert(strcmp(token, "y0_AgAAAAA") == 0);
}

static void test_pending_is_the_normal_answer_not_a_failure(void)
{
    /* Every poll before the user confirms comes back as HTTP 400 with this
     * body. Treating it as an error would abandon the flow immediately. */
    char token[YANDEX_AUTH_TOKEN_MAX + 1] = "leftover";
    assert(yandex_auth_parse_token("{\"error\":\"authorization_pending\","
                                   "\"error_description\":\"...\"}",
                                   token, sizeof(token)) == YANDEX_AUTH_TOKEN_PENDING);
    assert(token[0] == '\0');
}

static void test_the_other_oauth_verdicts_are_distinguished(void)
{
    char token[YANDEX_AUTH_TOKEN_MAX + 1];
    assert(yandex_auth_parse_token("{\"error\":\"slow_down\"}", token, sizeof(token)) ==
           YANDEX_AUTH_TOKEN_SLOW_DOWN);
    assert(yandex_auth_parse_token("{\"error\":\"access_denied\"}", token, sizeof(token)) ==
           YANDEX_AUTH_TOKEN_DENIED);
    assert(yandex_auth_parse_token("{\"error\":\"expired_token\"}", token, sizeof(token)) ==
           YANDEX_AUTH_TOKEN_EXPIRED);
    assert(yandex_auth_parse_token("{\"error\":\"invalid_grant\"}", token, sizeof(token)) ==
           YANDEX_AUTH_TOKEN_EXPIRED);
    assert(yandex_auth_parse_token("{\"error\":\"invalid_client\"}", token, sizeof(token)) ==
           YANDEX_AUTH_TOKEN_INVALID);
}

static void test_a_credential_is_never_salvaged_from_a_bad_answer(void)
{
    char token[YANDEX_AUTH_TOKEN_MAX + 1] = "leftover";
    assert(yandex_auth_parse_token("garbage", token, sizeof(token)) ==
           YANDEX_AUTH_TOKEN_INVALID);
    assert(token[0] == '\0');
    assert(yandex_auth_parse_token(NULL, token, sizeof(token)) == YANDEX_AUTH_TOKEN_INVALID);
    assert(yandex_auth_parse_token("{}", token, sizeof(token)) == YANDEX_AUTH_TOKEN_INVALID);
    assert(yandex_auth_parse_token("{\"access_token\":\"\"}", token, sizeof(token)) ==
           YANDEX_AUTH_TOKEN_INVALID);
    assert(yandex_auth_parse_token("{\"access_token\":123}", token, sizeof(token)) ==
           YANDEX_AUTH_TOKEN_INVALID);
    assert(yandex_auth_parse_token("{\"access_token\":\"x\"}", NULL, 16U) ==
           YANDEX_AUTH_TOKEN_INVALID);
}

static void test_a_token_too_long_for_the_buffer_is_refused(void)
{
    char json[64];
    char token[8];
    snprintf(json, sizeof(json), "{\"access_token\":\"%s\"}", "0123456789");
    assert(yandex_auth_parse_token(json, token, sizeof(token)) == YANDEX_AUTH_TOKEN_INVALID);
    assert(token[0] == '\0');
}

int main(void)
{
    test_the_real_answer_yields_everything_the_screen_needs();
    test_a_missing_field_fails_the_whole_answer();
    test_missing_timings_fall_back_rather_than_fail();
    test_an_overlong_field_is_refused_not_truncated();
    test_the_confirmed_answer_hands_over_the_token();
    test_pending_is_the_normal_answer_not_a_failure();
    test_the_other_oauth_verdicts_are_distinguished();
    test_a_credential_is_never_salvaged_from_a_bad_answer();
    test_a_token_too_long_for_the_buffer_is_refused();
    printf("yandex_auth_json tests passed\n");
    return 0;
}
