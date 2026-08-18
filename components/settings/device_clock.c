#include "device_clock.h"

#ifdef ESP_PLATFORM

#include <time.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"

static const char *TAG = "device_clock";
static bool s_started;

void device_clock_init(void)
{
    if (s_started) return;

    /* The pool is the one thing here that is a choice rather than a fact:
     * pool.ntp.org resolves to whatever is close, needs no account, and is
     * what every appliance uses. */
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    /* Start regardless of the network: SNTP retries by itself, and gating this
     * on a connection would mean re-plumbing it through the Wi-Fi events for
     * no benefit. Until a reply arrives the clock simply reads unset. */
    config.start = true;
    const esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SNTP init failed: %s", esp_err_to_name(err));
        return;
    }
    s_started = true;

    /* Moscow. Written as a POSIX rule with no DST clause because Russia does
     * not observe it; a plain "MSK-3" would be wrong for anywhere else, so
     * this is the line to change when the device travels. */
    setenv("TZ", "MSK-3", 1);
    tzset();
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
void device_clock_init(void) {}

bool device_clock_now(int *hour, int *minute)
{
    (void)hour;
    (void)minute;
    return false;
}

#endif /* ESP_PLATFORM */
