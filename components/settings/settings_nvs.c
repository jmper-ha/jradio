#include "settings_nvs.h"

#ifdef ESP_PLATFORM

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "settings_nvs";
#define SETTINGS_NVS_NAMESPACE "jradio"

/* NVS is brought up by Wi-Fi, which starts after the settings are first
 * read at boot; asking here as well costs nothing when it is already up.
 * The two errors that need an erase are handled the way Wi-Fi handles them,
 * so whichever comes first leaves NVS the same. */
static bool settings_nvs_ready(void)
{
    static bool ready;
    if (ready) return true;
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs an erase: %s", esp_err_to_name(err));
        if (nvs_flash_erase() == ESP_OK) err = nvs_flash_init();
    }
    ready = err == ESP_OK;
    if (!ready) ESP_LOGE(TAG, "NVS unavailable: %s", esp_err_to_name(err));
    return ready;
}

bool settings_nvs_get_u8(const char *key, uint8_t *value)
{
    if (key == NULL || value == NULL || !settings_nvs_ready()) return false;
    nvs_handle_t handle;
    if (nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return false;
    const bool found = nvs_get_u8(handle, key, value) == ESP_OK;
    nvs_close(handle);
    return found;
}

bool settings_nvs_set_u8(const char *key, uint8_t value)
{
    if (key == NULL || !settings_nvs_ready()) return false;
    nvs_handle_t handle;
    if (nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
    uint8_t stored = 0U;
    /* The same value again writes nothing: NVS appends an entry per set,
     * and a page fills - and has to be erased - sooner for every one. */
    esp_err_t err = ESP_OK;
    if (nvs_get_u8(handle, key, &stored) != ESP_OK || stored != value) {
        err = nvs_set_u8(handle, key, value);
        if (err == ESP_OK) err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) ESP_LOGW(TAG, "cannot store %s: %s", key, esp_err_to_name(err));
    return err == ESP_OK;
}

void settings_nvs_erase(const char *key)
{
    if (key == NULL || !settings_nvs_ready()) return;
    nvs_handle_t handle;
    if (nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;
    if (nvs_erase_key(handle, key) == ESP_OK) (void)nvs_commit(handle);
    nvs_close(handle);
}

#else

bool settings_nvs_get_u8(const char *key, uint8_t *value)
{
    (void)key;
    (void)value;
    return false;
}

bool settings_nvs_set_u8(const char *key, uint8_t value)
{
    (void)key;
    (void)value;
    return false;
}

void settings_nvs_erase(const char *key)
{
    (void)key;
}

#endif
