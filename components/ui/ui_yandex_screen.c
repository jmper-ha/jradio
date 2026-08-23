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

/* Linked, so the screen is about stations from here on. */
static void ui_yandex_build_catalog(yandex_catalog_state_t catalog_state,
                                    size_t station_count, ui_yandex_view_t *view)
{
    if (catalog_state == YANDEX_CATALOG_READY && station_count > 0U) {
        view->mode = UI_YANDEX_MODE_LIST;
        view->status = "";
        /* No "OK - слушать" yet: promising playback the firmware cannot do
         * would be worse than saying nothing about that button. */
        view->hint = "F2 - назад, F4 - отвязать";
        return;
    }
    view->mode = UI_YANDEX_MODE_MESSAGE;
    switch (catalog_state) {
    case YANDEX_CATALOG_LOADING:
        view->status = "Загрузка станций...";
        view->hint = "F2 - назад";
        break;
    case YANDEX_CATALOG_FAILED:
        view->status = "Не удалось получить станции";
        view->hint = "OK - повторить, F2 - назад";
        break;
    case YANDEX_CATALOG_READY:
        /* Linked, answered, and empty. Not a failure, and retrying is still
         * the only thing that could change it. */
        view->status = "Станций нет";
        view->hint = "OK - обновить, F2 - назад";
        break;
    case YANDEX_CATALOG_EMPTY:
    default:
        view->status = "Аккаунт привязан";
        view->hint = "OK - обновить, F2 - назад, F4 - отвязать";
        break;
    }
}

void ui_yandex_view_build(const yandex_auth_status_t *status,
                          yandex_catalog_state_t catalog_state, size_t station_count,
                          ui_yandex_view_t *view)
{
    if (view == NULL) return;
    *view = (ui_yandex_view_t){
        .mode = UI_YANDEX_MODE_PAIRING,
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
        ui_yandex_build_catalog(catalog_state, station_count, view);
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
