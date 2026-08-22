#include "sd_storage.h"

#include <dirent.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sd_protocol_defs.h"
#include "sdmmc_cmd.h"

#include "board_options.h"

static const char *TAG = "sd";

/* The driver's host enum is not numbered by the peripheral, so the board's
 * plain number is mapped here - the same way board.c maps the display's. */
#if SDC_SPI_PERIPHERAL == 2
#define SD_SPI_HOST SPI2_HOST
#elif SDC_SPI_PERIPHERAL == 3
#define SD_SPI_HOST SPI3_HOST
#else
#error "SDC_SPI_PERIPHERAL must select SPI2 or SPI3"
#endif

#define SD_ROOT_LOG_MAX_ROWS 32

static sdmmc_card_t *s_card;
static bool s_bus_ready;
static volatile bool s_mounted;
// Mount and unmount can be asked for from different tasks - the browser opening
// the source, the player letting it go - and neither may run while the other is
// halfway through.
static SemaphoreHandle_t s_lock;

bool sd_storage_is_mounted(void)
{
    return s_mounted;
}

/* Proves the filesystem reads, and no more than that: the browser gets its own
 * listing later. Capped because the root of a card can hold thousands of
 * files, and the whole log goes out of a 115200 baud UART. */
static void log_root(void)
{
    DIR *dir = opendir(SD_STORAGE_ROOT_PATH);
    if (dir == NULL) {
        ESP_LOGE(TAG, "%s mounted but will not open", SD_STORAGE_ROOT_PATH);
        return;
    }
    ESP_LOGI(TAG, "files on %s:", SD_STORAGE_ROOT_PATH);
    unsigned int shown = 0U;
    unsigned int total = 0U;
    const struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        ++total;
        if (shown < SD_ROOT_LOG_MAX_ROWS) {
            // d_type is trusted here and nowhere else: this line is a report,
            // not a decision about what can be opened or played.
            ESP_LOGI(TAG, "  %s%s", entry->d_type == DT_DIR ? "[dir] " : "      ",
                     entry->d_name);
            ++shown;
        }
    }
    closedir(dir);
    if (total > shown) {
        ESP_LOGI(TAG, "  ... and %u more", total - shown);
    }
}

static esp_err_t (*s_card_transaction)(int slot, sdmmc_command_t *cmd);

/* Lets a card that refuses CMD59 (CRC_ON_OFF) finish initializing.
 *
 * The card fitted here is a 2 GB SDSC: it answers CMD0, rejects CMD8 as a
 * pre-2.00 card should, and rejects CMD59 too, which sdmmc_card_init treats as
 * fatal and reports as "no card". CMD59 only switches on the optional CRC16 of
 * SPI *data* transfers - command CRC7 is always on and unaffected - so a card
 * that will not enable it is still a card that reads correctly. Refusing to
 * mount over an optional feature is the driver's choice, not the spec's.
 *
 * Scoped to that one opcode and that one error: any other command, and any
 * other failure of this one, still fails the mount. */
static esp_err_t sd_transaction(int slot, sdmmc_command_t *cmd)
{
    const esp_err_t err = s_card_transaction(slot, cmd);
    if (cmd->opcode == SD_CRC_ON_OFF &&
        (err == ESP_ERR_NOT_SUPPORTED || cmd->error == ESP_ERR_NOT_SUPPORTED)) {
        ESP_LOGW(TAG, "card rejected CMD59; data CRC stays off");
        cmd->error = ESP_OK;
        return ESP_OK;
    }
    return err;
}

static esp_err_t sd_mount(void)
{
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST;
    // The host config counts in kHz; the board declares the wiring's limit in
    // Hz, like every other clock there.
    host.max_freq_khz = SDC_CLOCK_HZ / 1000;
    s_card_transaction = host.do_transaction;
    host.do_transaction = sd_transaction;

    sdspi_device_config_t device = SDSPI_DEVICE_CONFIG_DEFAULT();
    device.host_id = SD_SPI_HOST;
    device.gpio_cs = SDC_CS_GPIO;

    const esp_vfs_fat_mount_config_t mount = {
        /* Never, under any circumstance. This flag formats the user's card the
         * first time a filesystem it cannot read shows up - a card written on
         * a PC, or one that simply needs its FAT repaired - and there is no
         * undo. A card that will not mount is a message on screen, not a
         * reason to erase it. */
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 8192,
    };
    return esp_vfs_fat_sdspi_mount(SD_STORAGE_ROOT_PATH, &host, &device, &mount, &s_card);
}

static void log_card(void)
{
    ESP_LOGI(TAG, "card mounted at %s: %.*s, %llu MB, %s at %d kHz", SD_STORAGE_ROOT_PATH,
             (int)sizeof(s_card->cid.name), s_card->cid.name,
             ((uint64_t)s_card->csd.capacity * (uint64_t)s_card->csd.sector_size) /
                 (1024ULL * 1024ULL),
             s_card->is_mmc ? "MMC" : "SD", s_card->real_freq_khz);
}

// Says which of the two failures it was in the log; the return value carries
// the same distinction to the caller.
static void log_mount_failure(esp_err_t err)
{
    if (err == ESP_FAIL) {
        /* The card answered, so it is in the slot and the wiring works - only
         * its filesystem did not mount. Almost always exFAT: FATFS is built
         * without it (FF_FS_EXFAT is 0), and that is what a card over 32 GB
         * gets from a PC unless it is told otherwise. */
        ESP_LOGW(TAG, "a card is in the slot but its filesystem would not mount; "
                      "FAT16/FAT32 only, exFAT is not supported");
    } else {
        // No answer at all: an empty slot looks exactly like a card that is not
        // seated, or a bus that is miswired.
        ESP_LOGI(TAG, "no card answered on the SPI bus: %s", esp_err_to_name(err));
    }
}

esp_err_t sd_storage_mount(void)
{
    if (!s_bus_ready || s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = ESP_OK;
    if (s_card == NULL) {
        err = sd_mount();
        if (err == ESP_OK) {
            s_mounted = true;
            log_card();
        } else {
            log_mount_failure(err);
        }
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t sd_storage_unmount(void)
{
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = ESP_OK;
    if (s_card != NULL) {
        // Cleared first: from here on nothing new starts reading the card, and
        // whoever was reading it has already stopped - that is the caller's
        // half of the contract.
        s_mounted = false;
        err = esp_vfs_fat_sdcard_unmount(SD_STORAGE_ROOT_PATH, s_card);
        s_card = NULL;
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "unmount failed: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "card released");
        }
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t sd_storage_init(void)
{
    // Guarded on the bus, not on the card: the card comes and goes now, and
    // initializing the same SPI host twice fails.
    if (s_bus_ready) {
        return ESP_OK;
    }
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    const spi_bus_config_t bus = {
        .mosi_io_num = SDC_MOSI_GPIO,
        .miso_io_num = SDC_MISO_GPIO,
        .sclk_io_num = SDC_SCK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        // A FAT sector is 512 bytes and the driver reads a few at a time; this
        // is the ceiling on one DMA transfer, not a buffer that gets allocated.
        .max_transfer_sz = 4096,
    };
    const esp_err_t bus_result = spi_bus_initialize(SD_SPI_HOST, &bus, SPI_DMA_CH_AUTO);
    if (bus_result != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(bus_result));
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return bus_result;
    }
    /* A card holds MISO high through its own pull-up only while it is selected.
     * Between transfers the line floats, and a floating input reads back as
     * random bytes during identification. The internal pull-up is weak (~45 k)
     * and no substitute for a resistor on the board, but it costs nothing and
     * makes the difference on wiring that has none. */
    (void)gpio_set_pull_mode(SDC_MISO_GPIO, GPIO_PULLUP_ONLY);
    s_bus_ready = true;

    /* One probe, then let it go. Nothing needs the card this early, but a line
     * in the boot log saying whether the slot, the bus and the filesystem work
     * is worth 110 ms - the alternative is finding out only when someone opens
     * the source and gets an error with no history behind it. */
    if (sd_storage_mount() == ESP_OK) {
        log_root();
        (void)sd_storage_unmount();
    }
    // The mount is a feature; the bus being up is what this call promises.
    return ESP_OK;
}
