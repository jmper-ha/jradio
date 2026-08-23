#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ui_yandex_screen.h"

static void test_an_unlinked_device_offers_to_link(void)
{
    const yandex_auth_status_t status = {.state = YANDEX_AUTH_IDLE};
    ui_yandex_view_t view;
    ui_yandex_view_build(&status, &view);
    assert(strcmp(view.status, "Аккаунт не привязан") == 0);
    assert(!view.show_code);
    assert(view.countdown[0] == '\0');
    assert(strstr(view.hint, "привязать") != NULL);
    assert(!ui_yandex_view_is_busy(&status));
}

static void test_a_live_code_is_shown_with_its_address_and_countdown(void)
{
    yandex_auth_status_t status = {.state = YANDEX_AUTH_WAITING, .seconds_left = 287U};
    snprintf(status.user_code, sizeof(status.user_code), "gm2anfv7");
    snprintf(status.verification_url, sizeof(status.verification_url), "https://ya.ru/device");
    ui_yandex_view_t view;
    ui_yandex_view_build(&status, &view);
    assert(view.show_code);
    assert(strcmp(view.code, "gm2anfv7") == 0);
    assert(strcmp(view.url, "https://ya.ru/device") == 0);
    assert(strcmp(view.countdown, "Осталось 287 с") == 0);
    assert(ui_yandex_view_is_busy(&status));
}

static void test_a_code_without_an_address_is_not_shown_at_all(void)
{
    /* Half the pair is worse than none: the user would read out a code with
     * nowhere to type it and conclude the device is broken. */
    yandex_auth_status_t status = {.state = YANDEX_AUTH_WAITING, .seconds_left = 100U};
    snprintf(status.user_code, sizeof(status.user_code), "gm2anfv7");
    ui_yandex_view_t view;
    ui_yandex_view_build(&status, &view);
    assert(!view.show_code);
    assert(view.code[0] == '\0');
    assert(view.countdown[0] == '\0');
}

static void test_the_countdown_disappears_rather_than_showing_zero(void)
{
    yandex_auth_status_t status = {.state = YANDEX_AUTH_WAITING, .seconds_left = 0U};
    snprintf(status.user_code, sizeof(status.user_code), "gm2anfv7");
    snprintf(status.verification_url, sizeof(status.verification_url), "https://ya.ru/device");
    ui_yandex_view_t view;
    ui_yandex_view_build(&status, &view);
    assert(view.show_code);
    assert(view.countdown[0] == '\0');
}

static void test_a_linked_account_offers_only_to_unlink(void)
{
    const yandex_auth_status_t status = {.state = YANDEX_AUTH_AUTHORIZED};
    ui_yandex_view_t view;
    ui_yandex_view_build(&status, &view);
    assert(strcmp(view.status, "Аккаунт привязан") == 0);
    assert(!view.show_code);
    assert(strstr(view.hint, "отвязать") != NULL);
    assert(!ui_yandex_view_is_busy(&status));
}

static void test_every_failure_names_itself_and_offers_a_retry(void)
{
    /* "Ошибка" alone would leave the user guessing between a dead network, a
     * code they were too slow with, and a refusal on the phone. */
    static const struct {
        yandex_auth_error_t error;
        const char *fragment;
    } cases[] = {
        {YANDEX_AUTH_ERROR_NETWORK, "связи"},
        {YANDEX_AUTH_ERROR_TIMEOUT, "истёк"},
        {YANDEX_AUTH_ERROR_DENIED, "подтверждён"},
        {YANDEX_AUTH_ERROR_STORAGE, "сохранить"},
        {YANDEX_AUTH_ERROR_SERVER, "сервера"},
    };
    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        const yandex_auth_status_t status = {.state = YANDEX_AUTH_FAILED,
                                             .error = cases[index].error};
        ui_yandex_view_t view;
        ui_yandex_view_build(&status, &view);
        assert(strstr(view.status, cases[index].fragment) != NULL);
        assert(strstr(view.hint, "повторить") != NULL);
        assert(!view.show_code);
    }
}

static void test_a_request_in_flight_says_so_and_can_be_cancelled(void)
{
    const yandex_auth_status_t status = {.state = YANDEX_AUTH_REQUESTING};
    ui_yandex_view_t view;
    ui_yandex_view_build(&status, &view);
    assert(strstr(view.status, "Запрашиваем") != NULL);
    assert(strstr(view.hint, "отмена") != NULL);
    assert(ui_yandex_view_is_busy(&status));
}

static void test_bad_arguments_are_survivable(void)
{
    ui_yandex_view_t view;
    ui_yandex_view_build(NULL, &view);
    /* Still a complete view, so the screen has something to draw rather than
     * dereferencing a null string. */
    assert(view.status != NULL && view.hint != NULL && view.code != NULL &&
           view.url != NULL);
    ui_yandex_view_build(NULL, NULL);
    assert(!ui_yandex_view_is_busy(NULL));
}

int main(void)
{
    test_an_unlinked_device_offers_to_link();
    test_a_live_code_is_shown_with_its_address_and_countdown();
    test_a_code_without_an_address_is_not_shown_at_all();
    test_the_countdown_disappears_rather_than_showing_zero();
    test_a_linked_account_offers_only_to_unlink();
    test_every_failure_names_itself_and_offers_a_retry();
    test_a_request_in_flight_says_so_and_can_be_cancelled();
    test_bad_arguments_are_survivable();
    printf("ui_yandex_screen tests passed\n");
    return 0;
}
