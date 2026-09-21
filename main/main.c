#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_sleep.h"

#include "alarm_schedule.h"
#include "board.h"
#include "board_features.h"
#include "bt_link.h"
#include "board_input.h"
#include "device_clock.h"
#include "weather.h"
#include "device_settings.h"
#include "internet_radio.h"
#include "player_control.h"
#include "remote_control.h"
#include "sd_storage.h"
#include "settings_csv.h"
#include "system_report.h"
#include "ui.h"
#include "file_player.h"
#include "usb_storage.h"
#include "web_server.h"
#include "wifi_provisioning.h"
#include "wifi_settings.h"
#include "yandex_auth.h"
#include "yandex_catalog.h"
#include "yandex_feedback.h"

static const char *TAG = "jradio";

/* A subsystem the device is still usable without.
 *
 * ESP_ERROR_CHECK aborts, and an abort in app_main is a reboot into the same
 * failure: the device loops instead of running. That is the right trade for
 * the display or the player core, which leave nothing behind them, and the
 * wrong one for a drive that did not enumerate or a web server that could not
 * take its socket - losing those costs a feature, while looping costs
 * everything, including the half that still worked. */
static void start_optional(const char *what, esp_err_t result)
{
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "%s unavailable: %s", what, esp_err_to_name(result));
    }
}

/* How long the quiet wake gives a time server before it gives up and works
 * with the clock the RTC kept. Long enough for Wi-Fi to associate and one
 * round trip, short enough that a network that is simply gone does not eat the
 * lead the hop was given. */
#define ALARM_SYNC_WAIT_MS 15000U

/* The alarm's quiet wake-up, and the only reason anything here runs before the
 * board exists.
 *
 * A board sleeping until the morning counts the night on its internal RC
 * oscillator, which is worth minutes by dawn. So the sleep is taken in hops:
 * this one wakes ten minutes early, brings up nothing but Wi-Fi, asks a time
 * server what the hour really is, and either goes straight back down until a
 * minute before the alarm or - if the corrected clock says the alarm is nearly
 * here - lets the ordinary boot carry on. The panel is never lit and the
 * peripheral rail is never raised: on a board that wires PERIPHERAL_POWER_GPIO
 * it is still held low from the sleep before this one.
 *
 * Returns true when Wi-Fi and the clock have been started, so app_main does
 * not start either of them twice. */
static bool alarm_quiet_wake(const device_settings_t *settings, bool settings_read)
{
    if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER) return false;
    if (!settings_read || !alarm_config_valid(&settings->alarm)) {
        /* The timer fired but there is nothing to ring: the alarm was switched
         * off through the web while the board slept. An ordinary boot, which
         * is a device on its home screen rather than one that quietly went
         * back to sleep with no way to tell. */
        ESP_LOGW(TAG, "woken by the alarm timer with no alarm set; booting");
        return false;
    }

    ESP_LOGI(TAG, "alarm check: bringing up Wi-Fi to correct the clock");
    start_optional("Wi-Fi", wifi_provisioning_init());
    start_optional("Wi-Fi", wifi_provisioning_start());
    device_clock_init(settings->ntp_server, settings->timezone);
    /* A failure here is not fatal: the clock the board woke with is the one it
     * went to sleep with plus however far the oscillator drifted, which is
     * still close enough to ring on. */
    (void)device_clock_wait_sync(ALARM_SYNC_WAIT_MS);

    int weekday = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    const bool clock_valid = device_clock_moment(&weekday, &hour, &minute, &second);
    uint32_t ahead = 0U;
    if (clock_valid) {
        (void)alarm_schedule_next_seconds(&settings->alarm, weekday, hour, minute, second,
                                          &ahead);
    }
    uint32_t sleep_seconds = 0U;
    const alarm_boot_action_t action =
        alarm_boot_decide(&settings->alarm, clock_valid, ahead, &sleep_seconds);
    if (action == ALARM_BOOT_SLEEP_AGAIN) {
        ESP_LOGI(TAG, "alarm in %u s; sleeping %u s more", (unsigned int)ahead,
                 (unsigned int)sleep_seconds);
        /* Or the radio going down raises a disconnect that the reconnect
         * machinery answers by bringing Wi-Fi straight back up. */
        (void)wifi_provisioning_stop();
        board_deep_sleep_again(sleep_seconds);  /* does not return */
    }
    if (action == ALARM_BOOT_PROCEED) {
        ESP_LOGI(TAG, "alarm in %u s; staying up for it", (unsigned int)ahead);
        alarm_boot_mark_pending();
    }
    return true;
}

/* The other quiet wake: the infrared receiver's pin. Any remote in the room
 * wakes the chip - the television's, pointed elsewhere - so the boot stops
 * here, listens for a moment, and goes back down unless what came was our
 * Power key. Nothing is lit and nothing is started; the timer for the alarm
 * is re-armed from the clock the RTC kept through the sleep, drift and all,
 * because the ten-minutes-early hop corrects that anyway. Like the alarm's
 * check, this either does not return or lets the boot carry on. */
static void remote_quiet_wake(const device_settings_t *settings, bool settings_read)
{
    if (!board_woke_by_remote()) return;
    if (remote_control_wake_check()) {
        ESP_LOGI(TAG, "woken by the remote's Power key");
        return;
    }
    ESP_LOGI(TAG, "woken by the receiver, but not by our Power key; sleeping on");
    uint32_t wake_after = 0U;
    if (settings_read) {
        device_clock_set_timezone(settings->timezone);
        int weekday = 0;
        int hour = 0;
        int minute = 0;
        int second = 0;
        const bool clock_valid = device_clock_moment(&weekday, &hour, &minute, &second);
        wake_after = alarm_sleep_wake_after_seconds(&settings->alarm, clock_valid, weekday, hour,
                                                    minute, second);
    }
    board_deep_sleep_again(wake_after);  /* does not return */
}

static void input_log_task(void *arg)
{
    (void)arg;
    board_input_action_t action;
    while (true) {
        if (board_input_read(&action, portMAX_DELAY)) {
            (void)ui_post_input(action);
        }
    }
}

void app_main(void)
{
    // First, so the reset reason is the first thing in the log after a crash,
    // and before anything allocates, so the boot heap figure means something.
    system_report_boot();
    /* Before everything else, when it was the receiver that woke the chip:
     * every millisecond the receiver is not listening is a millisecond of the
     * second press of Power it can miss. */
    if (board_woke_by_remote()) remote_control_wake_listen();
    // Before anything that could write settings: the lock it creates has to
    // exist by the time player_control and ui are running, and app_main is
    // still the only task at this point.
    settings_csv_init();
    /* The settings are read here, before the board, for three of them: the
     * boot splash is drawn at the end of board_init(), the panel's flip is the
     * one thing that cannot be applied to a picture after it has been drawn,
     * and its colour inversion would show the splash as a negative first.
     * The mount is what settings.csv lives on and happens anyway a moment
     * later, inside Wi-Fi; asking for it early is free, it is idempotent, and
     * the screen is still black at this point - the backlight comes up only
     * once the splash is on the glass. If either the mount or the file fails,
     * the flips read as off, which is the panel's own baseline. */
    device_settings_t boot_settings;
    const bool settings_read =
        wifi_settings_storage_init() == ESP_OK && device_settings_init(&boot_settings);
    if (!settings_read) {
        ESP_LOGW(TAG, "settings unreadable at boot; splash drawn unflipped");
    }
    /* Before the board, and the only thing that is: this may not be a boot at
     * all but a hop in the night, and a hop that lights the panel has already
     * failed at being quiet. It either does not return or leaves the ordinary
     * boot to carry on below. */
    const bool network_started = alarm_quiet_wake(&boot_settings, settings_read);
    remote_quiet_wake(&boot_settings, settings_read);
    // Fatal on purpose: without the board there is no screen, no sound and no
    // controls, and without player_control and the UI there is nothing to
    // drive them with. A reboot loop is at least an honest signal there.
    ESP_ERROR_CHECK(board_init(settings_read && boot_settings.flip_vertical,
                               settings_read && boot_settings.flip_horizontal,
                               settings_read && boot_settings.invert_colors,
                               alarm_boot_pending()));
    /* Both may already be up: the alarm check above needs the network and the
     * clock before the board exists, and neither is started twice. */
    if (!network_started) {
        start_optional("Wi-Fi", wifi_provisioning_init());
        start_optional("Wi-Fi", wifi_provisioning_start());
        // After Wi-Fi is up so the first query has somewhere to go, but it does
        // not depend on being connected - SNTP retries on its own and the clock
        // reads unset until an answer arrives.
        /* The zone and the server come off the card, and a card that would not
         * read leaves both at their defaults - a clock on Moscow time is what
         * this device had before either was a setting. */
        device_clock_init(settings_read ? boot_settings.ntp_server : NULL,
                          settings_read ? boot_settings.timezone : NULL);
    }
    /* Same place and the same reason: it waits for the network on its own,
     * and it reads its service and its coordinates off the same card. */
    start_optional("weather", weather_init(settings_read ? &boot_settings : NULL));
    /* Each of these belongs to a part or a feature board_options.h can leave
     * out, so each is asked for only when this build has it. A plain `if` on
     * a constant rather than #if: the call still has to compile, which is what
     * catches a signature drifting away from a subsystem nobody currently
     * builds. */
    if (BOARD_HAS_YANDEX_MUSIC) {
        // After Wi-Fi, because that is what mounts LittleFS, and before the UI
        // so the menu already knows whether an account is linked when it first
        // draws.
        start_optional("Yandex Music", yandex_auth_init());
        start_optional("Yandex Music catalog", yandex_catalog_init());
        /* Costs a mutex until a station is actually played; what it buys is
         * that the rotor learns what was listened to, so a station opened
         * tomorrow does not start where it started today. */
        start_optional("Yandex Music feedback", yandex_feedback_init());
    }
    if (BOARD_HAS_SD_CARD) {
        // Before the UI and the radio, deliberately: sd_storage_init()
        // explains what the internal-SRAM heap does if the mount lands after
        // them.
        start_optional("SD card", sd_storage_init());
    }
    if (BOARD_HAS_USB) {
        start_optional("USB storage", usb_storage_init());
    }
    if (BOARD_HAS_USB || BOARD_HAS_SD_CARD) {
        // One player for both volumes, so it is wanted if either is fitted.
        start_optional("file player", file_player_init());
    }
    if (BOARD_HAS_BLUETOOTH) {
        /* Before the player, which asks the link whether the module is
         * there every time it builds a snapshot; the module itself may be
         * booting still, and shows up in the source list when it answers. */
        start_optional("Bluetooth module link", bt_link_init());
    }
    ESP_ERROR_CHECK(internet_radio_init());
    /* Nothing is installed here any more. A station that arrives one track at
     * a time names what feeds it when it starts - see
     * internet_radio_start_track_chain() - because there are two such sources
     * and one seam, and a feeder left over from boot is one the other source
     * inherits. */
    ESP_ERROR_CHECK(player_control_init());
    ESP_ERROR_CHECK(ui_init());
    start_optional("web server", web_server_start());
    player_snapshot_t snapshot;
    player_control_get_snapshot(&snapshot);
    ESP_LOGI(TAG, "jradio booted; active audio source=%d",
             (int)snapshot.active_source);
    ESP_ERROR_CHECK(xTaskCreate(input_log_task, "input_log", 3072, NULL, 4, NULL) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
}
