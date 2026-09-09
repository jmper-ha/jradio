#include "ui_yandex_screen.h"

#include <stdio.h>

static const char *ui_yandex_failure_text(yandex_auth_error_t error,
                                          device_language_t language)
{
    device_text_id_t id;
    switch (error) {
    case YANDEX_AUTH_ERROR_NETWORK: id = DEVICE_TEXT_YANDEX_NO_CONNECTION; break;
    case YANDEX_AUTH_ERROR_TIMEOUT: id = DEVICE_TEXT_YANDEX_CODE_EXPIRED; break;
    case YANDEX_AUTH_ERROR_DENIED: id = DEVICE_TEXT_YANDEX_DENIED; break;
    case YANDEX_AUTH_ERROR_STORAGE: id = DEVICE_TEXT_YANDEX_SAVE_FAILED; break;
    case YANDEX_AUTH_ERROR_SERVER:
    case YANDEX_AUTH_ERROR_NONE:
    default: id = DEVICE_TEXT_YANDEX_SERVER_ERROR; break;
    }
    return device_text(id, language);
}

/* Linked, so the screen is about stations from here on. */
static void ui_yandex_build_catalog(yandex_catalog_state_t catalog_state,
                                    size_t station_count, device_language_t language,
                                    ui_yandex_view_t *view)
{
    if (catalog_state == YANDEX_CATALOG_READY && station_count > 0U) {
        view->mode = UI_YANDEX_MODE_LIST;
        view->status = "";
        view->hint = device_text(DEVICE_TEXT_HINT_LISTEN, language);
        return;
    }
    view->mode = UI_YANDEX_MODE_MESSAGE;
    switch (catalog_state) {
    case YANDEX_CATALOG_LOADING:
        view->status = device_text(DEVICE_TEXT_YANDEX_LOADING, language);
        view->hint = device_text(DEVICE_TEXT_HINT_BACK, language);
        break;
    case YANDEX_CATALOG_FAILED:
        view->status = device_text(DEVICE_TEXT_YANDEX_LIST_FAILED, language);
        view->hint = device_text(DEVICE_TEXT_HINT_RETRY, language);
        break;
    case YANDEX_CATALOG_READY:
        /* Linked, answered, and empty. Not a failure, and retrying is still
         * the only thing that could change it. */
        view->status = device_text(DEVICE_TEXT_YANDEX_LIST_EMPTY, language);
        view->hint = device_text(DEVICE_TEXT_HINT_REFRESH, language);
        break;
    case YANDEX_CATALOG_EMPTY:
    default:
        view->status = device_text(DEVICE_TEXT_YANDEX_LINKED, language);
        view->hint = device_text(DEVICE_TEXT_HINT_REFRESH, language);
        break;
    }
}

void ui_yandex_view_build(const yandex_auth_status_t *status,
                          yandex_catalog_state_t catalog_state, size_t station_count,
                          device_language_t language, ui_yandex_view_t *view)
{
    if (view == NULL) return;
    *view = (ui_yandex_view_t){
        .mode = UI_YANDEX_MODE_PAIRING,
        .status = device_text(DEVICE_TEXT_YANDEX_NOT_LINKED, language),
        .code = "",
        .url = "",
        .hint = device_text(DEVICE_TEXT_HINT_LINK, language),
        .show_code = false,
    };
    view->countdown[0] = '\0';
    if (status == NULL) return;

    switch (status->state) {
    case YANDEX_AUTH_REQUESTING:
        view->status = device_text(DEVICE_TEXT_YANDEX_REQUESTING, language);
        view->hint = device_text(DEVICE_TEXT_HINT_CANCEL, language);
        break;
    case YANDEX_AUTH_WAITING:
        view->status = device_text(DEVICE_TEXT_YANDEX_ENTER_CODE, language);
        view->hint = device_text(DEVICE_TEXT_HINT_CANCEL, language);
        /* A code with no address to type it into is useless, and the address
         * comes from the server in the same answer - so either both or
         * neither. */
        view->show_code = status->user_code[0] != '\0' &&
                          status->verification_url[0] != '\0';
        if (view->show_code) {
            view->code = status->user_code;
            view->url = status->verification_url;
            if (status->seconds_left > 0U) {
                snprintf(view->countdown, sizeof(view->countdown),
                         device_text(DEVICE_TEXT_YANDEX_SECONDS_LEFT, language),
                         (unsigned int)status->seconds_left);
            }
        }
        break;
    case YANDEX_AUTH_AUTHORIZED:
        ui_yandex_build_catalog(catalog_state, station_count, language, view);
        break;
    case YANDEX_AUTH_FAILED:
        view->status = ui_yandex_failure_text(status->error, language);
        /* Deliberately the same hint as the idle state: a failure that offers
         * no way to try again is a dead end. */
        view->hint = device_text(DEVICE_TEXT_HINT_RETRY, language);
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
