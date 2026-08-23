#include "ui_yandex_screen.h"

#include <stdio.h>

static const char *ui_yandex_failure_text(yandex_auth_error_t error)
{
    switch (error) {
    case YANDEX_AUTH_ERROR_NETWORK:
        return "Нет связи с Яндексом";
    case YANDEX_AUTH_ERROR_TIMEOUT:
        return "Код истёк";
    case YANDEX_AUTH_ERROR_DENIED:
        return "Вход не подтверждён";
    case YANDEX_AUTH_ERROR_STORAGE:
        return "Не удалось сохранить";
    case YANDEX_AUTH_ERROR_SERVER:
    case YANDEX_AUTH_ERROR_NONE:
    default:
        return "Ошибка сервера";
    }
}

void ui_yandex_view_build(const yandex_auth_status_t *status, ui_yandex_view_t *view)
{
    if (view == NULL) return;
    *view = (ui_yandex_view_t){
        .status = "Аккаунт не привязан",
        .code = "",
        .url = "",
        .hint = "OK - привязать, F2 - назад",
        .show_code = false,
    };
    view->countdown[0] = '\0';
    if (status == NULL) return;

    switch (status->state) {
    case YANDEX_AUTH_REQUESTING:
        view->status = "Запрашиваем код...";
        view->hint = "F2 - отмена";
        break;
    case YANDEX_AUTH_WAITING:
        view->status = "Введите код на сайте";
        view->hint = "F2 - отмена";
        /* A code with no address to type it into is useless, and the address
         * comes from the server in the same answer - so either both or
         * neither. */
        view->show_code = status->user_code[0] != '\0' &&
                          status->verification_url[0] != '\0';
        if (view->show_code) {
            view->code = status->user_code;
            view->url = status->verification_url;
            if (status->seconds_left > 0U) {
                snprintf(view->countdown, sizeof(view->countdown), "Осталось %u с",
                         (unsigned int)status->seconds_left);
            }
        }
        break;
    case YANDEX_AUTH_AUTHORIZED:
        view->status = "Аккаунт привязан";
        view->hint = "F4 - отвязать, F2 - назад";
        break;
    case YANDEX_AUTH_FAILED:
        view->status = ui_yandex_failure_text(status->error);
        /* Deliberately the same hint as the idle state: a failure that offers
         * no way to try again is a dead end. */
        view->hint = "OK - повторить, F2 - назад";
        break;
    case YANDEX_AUTH_IDLE:
    default:
        break;
    }
}

bool ui_yandex_view_is_busy(const yandex_auth_status_t *status)
{
    return status != NULL && (status->state == YANDEX_AUTH_REQUESTING ||
                              status->state == YANDEX_AUTH_WAITING);
}
