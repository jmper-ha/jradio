#include "ota_update.h"

#include <string.h>

#include "board_display_profile.h"
#include "esp_log.h"
#include "esp_image_format.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "player_control.h"

static const char *TAG = "ota";

/* Long enough for the page's next poll to read "restarting" rather than lose
 * the connection mid-question and report a failure. */
#define OTA_RESTART_DELAY_US (1500 * 1000)

/* This build's own mark, right after the app description where the next
 * build's check looks for it. Read below as the display an incoming image
 * must match - through a volatile read, because that read is the only thing
 * keeping --gc-sections from dropping the section: a plain one the compiler
 * folds to the constant, which left the first try of this without its mark. */
const __attribute__((section(".rodata_custom_desc"), used)) ota_image_mark_t ota_image_mark = {
    .magic = OTA_IMAGE_MARK_MAGIC,
    .display = DISPLAY,
    .reserved = 0,
};

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static ota_status_t s_status;
/* The writer's own, not esp_ota_begin/write/end: those refuse factory as a
 * place to write ("the staging partition cannot be of type Factory"), and a
 * device running from ota_0 has no other slot to go to. The first update
 * went factory -> ota_0 and passed; the second, back the other way, failed
 * before a byte was written.
 *
 * Only the HTTP worker writes these; the panel reaches s_target through
 * confirm/cancel only once the state says the writing is over. */
static bool s_writing;
static size_t s_written;
static size_t s_erased;
static const esp_partition_t *s_target;
static esp_timer_handle_t s_restart_timer;

static void set_state(ota_state_t state, const char *error)
{
    portENTER_CRITICAL(&s_lock);
    s_status.state = state;
    s_status.error = error;
    ++s_status.serial;
    portEXIT_CRITICAL(&s_lock);
}

void ota_update_get_status(ota_status_t *status)
{
    portENTER_CRITICAL(&s_lock);
    *status = s_status;
    portEXIT_CRITICAL(&s_lock);
}

const char *ota_update_running_slot(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    return running != NULL && running->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY ? "factory"
                                                                                    : "ota_0";
}

/* The app slot that is not running. */
static const esp_partition_t *other_slot(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_subtype_t wanted = running != NULL &&
                                                   running->subtype ==
                                                       ESP_PARTITION_SUBTYPE_APP_FACTORY
                                               ? ESP_PARTITION_SUBTYPE_APP_OTA_0
                                               : ESP_PARTITION_SUBTYPE_APP_FACTORY;
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP, wanted, NULL);
}

static void release_handle(void)
{
    s_writing = false;
}

esp_err_t ota_update_begin(const uint8_t *head, size_t head_length, size_t total,
                           ota_image_result_t *why)
{
    *why = OTA_IMAGE_OK;
    ota_status_t now;
    ota_update_get_status(&now);
    if (now.state == OTA_STATE_RESTARTING) return ESP_ERR_INVALID_STATE;
    /* Whatever an earlier upload left open, this one replaces it - a page
     * reloaded mid-upload never sends the end of the first one. */
    release_handle();

    ota_image_info_t info;
    const unsigned own_display = *(const volatile uint16_t *)&ota_image_mark.display;
    *why = ota_image_check(head, head_length, own_display, &info);
    if (*why != OTA_IMAGE_OK) {
        ESP_LOGW(TAG, "refused an image: %s (version '%s')", ota_image_result_code(*why),
                 info.version);
        return ESP_ERR_INVALID_ARG;
    }
    s_target = other_slot();
    if (s_target == NULL) return ESP_ERR_NOT_FOUND;
    if (total > s_target->size) {
        ESP_LOGW(TAG, "image of %u bytes does not fit the %u-byte slot", (unsigned)total,
                 (unsigned)s_target->size);
        return ESP_ERR_INVALID_SIZE;
    }

    /* The sound goes first. The flash is written with the cache off, and a
     * stream fed between erases only stutters; a stream starting a TLS
     * session beside it is also what internal RAM has least room for. */
    const player_command_t stop = {
        .kind = PLAYER_COMMAND_STOP_SOURCE,
        .source = AUDIO_SOURCE_NONE,
        .item_index = PLAYER_ITEM_NONE,
    };
    if (!player_control_post(&stop)) ESP_LOGW(TAG, "stop for the update was not queued");

    portENTER_CRITICAL(&s_lock);
    s_status.done = 0U;
    s_status.total = total;
    memcpy(s_status.version, info.version, sizeof(s_status.version));
    portEXIT_CRITICAL(&s_lock);
    set_state(OTA_STATE_RECEIVING, NULL);

    s_written = 0U;
    s_erased = 0U;
    s_writing = true;
    esp_err_t err = ota_update_write(head, head_length);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cannot start writing %s: %s", s_target->label, esp_err_to_name(err));
        ota_update_abort("flash");
        return err;
    }
    ESP_LOGI(TAG, "writing %s (%u bytes) to %s", info.version, (unsigned)total,
             s_target->label);
    return ESP_OK;
}

esp_err_t ota_update_write(const void *data, size_t length)
{
    if (!s_writing) return ESP_ERR_INVALID_STATE;
    if (length > s_target->size - s_written) return ESP_ERR_INVALID_SIZE;
    /* Each sector erased as the write reaches it, instead of three megabytes
     * up front - which held the flash, and with it every task running from
     * it, for tens of seconds. */
    esp_err_t err = ESP_OK;
    while (err == ESP_OK && s_erased < s_written + length) {
        err = esp_partition_erase_range(s_target, s_erased, s_target->erase_size);
        s_erased += s_target->erase_size;
    }
    if (err == ESP_OK) err = esp_partition_write(s_target, s_written, data, length);
    if (err == ESP_OK) {
        s_written += length;
        portENTER_CRITICAL(&s_lock);
        s_status.done += length;
        portEXIT_CRITICAL(&s_lock);
    }
    return err;
}

esp_err_t ota_update_finish(void)
{
    if (!s_writing) return ESP_ERR_INVALID_STATE;
    s_writing = false;
    /* The check esp_ota_end makes: the whole image - segments, checksum, the
     * appended SHA-256 - which a file cut short or garbled on the way does
     * not pass. */
    const esp_partition_pos_t where = {.offset = s_target->address, .size = s_target->size};
    esp_image_metadata_t metadata;
    const esp_err_t err = esp_image_verify(ESP_IMAGE_VERIFY, &where, &metadata);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "the written image does not verify: %s", esp_err_to_name(err));
        set_state(OTA_STATE_FAILED, "verify");
        return err;
    }
    ESP_LOGI(TAG, "image verified in %s; waiting for the encoder", s_target->label);
    set_state(OTA_STATE_CONFIRM, NULL);
    return ESP_OK;
}

void ota_update_abort(const char *error)
{
    release_handle();
    set_state(OTA_STATE_FAILED, error);
}

static void restart_now(void *arg)
{
    (void)arg;
    esp_restart();
}

esp_err_t ota_update_confirm(void)
{
    ota_status_t now;
    ota_update_get_status(&now);
    if (now.state != OTA_STATE_CONFIRM || s_target == NULL) return ESP_ERR_INVALID_STATE;
    const esp_err_t err = esp_ota_set_boot_partition(s_target);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cannot switch to %s: %s", s_target->label, esp_err_to_name(err));
        set_state(OTA_STATE_FAILED, "flash");
        return err;
    }
    ESP_LOGW(TAG, "booting %s from %s next; restarting", now.version, s_target->label);
    set_state(OTA_STATE_RESTARTING, NULL);
    if (s_restart_timer == NULL) {
        const esp_timer_create_args_t args = {.callback = restart_now, .name = "ota_restart"};
        if (esp_timer_create(&args, &s_restart_timer) != ESP_OK) esp_restart();
    }
    if (esp_timer_start_once(s_restart_timer, OTA_RESTART_DELAY_US) != ESP_OK) esp_restart();
    return ESP_OK;
}

void ota_update_cancel(void)
{
    ota_status_t now;
    ota_update_get_status(&now);
    if (now.state != OTA_STATE_CONFIRM) return;
    ESP_LOGI(TAG, "update to %s declined on the panel", now.version);
    set_state(OTA_STATE_IDLE, "declined");
}
