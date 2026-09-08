#include "device_clock.h"

#ifdef ESP_PLATFORM

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"

#include "device_settings.h"
#include "device_timezone.h"

static const char *TAG = "device_clock";
static bool s_started;
/* What SNTP was last given, so a settings write that did not touch the server
 * does not cost it a restart - and so a restart happens at all when it did. */
static char s_server[DEVICE_NTP_SERVER_MAX];

static void device_clock_set_zone(const char *timezone_id)
{
    /* A POSIX TZ rule, not a zone name: the C library has no tzdata on this
     * device. Anything the table does not know leaves the clock where it is
     * rather than on UTC, which would be a device three hours out with nothing
     * on screen to say why. */
    const device_timezone_t *zone = device_timezone_find(timezone_id);
    if (zone == NULL) zone = device_timezone_find(DEVICE_TIMEZONE_DEFAULT_ID);
    if (zone == NULL) return;
    setenv("TZ", zone->posix, 1);
    tzset();
}

static void device_clock_start_sntp(const char *server)
{
    const char *host = server == NULL || server[0] == '\0' ? DEVICE_NTP_SERVER_DEFAULT
                                                           : server;
    if (s_started && strcmp(s_server, host) == 0) return;
    if (s_started) {
        /* Torn down and built again rather than renamed in place: the config
         * carries the server list, and one call that certainly reaches every
         * copy of it beats two that might not. */
        esp_netif_sntp_deinit();
        s_started = false;
    }
    /* Copied here *before* the config is built, and the copy is what SNTP is
     * given: lwIP keeps the pointer it is handed rather than the characters,
     * so a name on somebody's stack is a name that turns to rubbish as soon as
     * that frame goes. Which is exactly what happened - the setting arrives
     * from app_main's own locals, SNTP started, said it was asking
     * pool.ntp.org, and never received an answer again. */
    snprintf(s_server, sizeof(s_server), "%s", host);
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(s_server);
    /* Start regardless of the network: SNTP retries by itself, and gating this
     * on a connection would mean re-plumbing it through the Wi-Fi events for
     * no benefit. Until a reply arrives the clock simply reads unset. */
    config.start = true;
    const esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SNTP init failed for %s: %s", host, esp_err_to_name(err));
        return;
    }
    s_started = true;
    ESP_LOGI(TAG, "asking %s for the time", s_server);
}

void device_clock_init(const char *server, const char *timezone_id)
{
    device_clock_apply(server, timezone_id);
}

void device_clock_apply(const char *server, const char *timezone_id)
{
    device_clock_set_zone(timezone_id);
    device_clock_start_sntp(server);
}

bool device_clock_now(int *hour, int *minute)
{
    if (hour == NULL || minute == NULL) return false;

    const time_t now = time(NULL);
    /* Before the first sync the clock sits near the epoch. Any year past 2020
     * means a real time was received - there is no other way for the counter
     * to get there. */
    if (now < 1600000000) return false;

    struct tm local;
    localtime_r(&now, &local);
    /* Logged once, at the moment the clock becomes usable: without it there is
     * no way to tell a failed sync from a screen that simply has not been
     * looked at, since the only other evidence is four characters on a panel. */
    static bool s_announced;
    if (!s_announced) {
        s_announced = true;
        ESP_LOGI(TAG, "clock synchronised: %04d-%02d-%02d %02d:%02d local",
                 local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour,
                 local.tm_min);
    }
    *hour = local.tm_hour;
    *minute = local.tm_min;
    return true;
}

#else

/* The host build has no SNTP and no need for one; the screen logic that reads
 * this is tested through ui_status_bar, which takes the time as an argument. */
void device_clock_init(const char *server, const char *timezone_id)
{
    (void)server;
    (void)timezone_id;
}

void device_clock_apply(const char *server, const char *timezone_id)
{
    (void)server;
    (void)timezone_id;
}

bool device_clock_now(int *hour, int *minute)
{
    (void)hour;
    (void)minute;
    return false;
}

#endif /* ESP_PLATFORM */
