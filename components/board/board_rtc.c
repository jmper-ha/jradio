#include "board_rtc.h"

#include <stdint.h>
#include <sys/time.h>
#include <time.h>

#include "board_config.h"
#include "board_i2c.h"
#include "board_parts.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "rtc_chip.h"

static const char *TAG = "rtc";

/* 100 kHz: the DS1307 goes no faster, and seven bytes at boot and once an
 * hour have no use for 400. */
#define RTC_I2C_HZ 100000U
#define RTC_I2C_TIMEOUT_MS 50
#define RTC_I2C_TRIES 3
// The system clock counts as set from here on - device_clock.c's rule.
#define RTC_CLOCK_SET_AFTER 1600000000

static i2c_master_dev_handle_t s_device;
static unsigned s_chip;
/* The boot read runs in app_main and the write in the SNTP callback, on
 * lwIP's task: one lock so they cannot interleave on the chip. */
static SemaphoreHandle_t s_lock;

static const char *chip_name(void)
{
    return s_chip == RTC_CHIP_DS1307 ? "DS1307" : "DS3231";
}

static bool open_chip(void)
{
    if (s_device != NULL) return true;
    s_chip = board_rtc_chip();
    if (s_chip == RTC_CHIP_NONE) return false;
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) return false;
    }
    i2c_master_bus_handle_t bus = NULL;
    esp_err_t err = board_i2c0_bus(&bus);
    if (err == ESP_OK) {
        const i2c_device_config_t config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = RTC_CHIP_I2C_ADDRESS,
            .scl_speed_hz = RTC_I2C_HZ,
        };
        err = i2c_master_bus_add_device(bus, &config, &s_device);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s: no I2C0: %s", chip_name(), esp_err_to_name(err));
        s_device = NULL;
        return false;
    }
    return true;
}

/* A few tries, as the tuner does: the bench's wires run beside I2S, whose bit
 * clock spoiled about one tuner read a second there. */
static esp_err_t read_registers(uint8_t first, uint8_t *bytes, size_t count)
{
    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < RTC_I2C_TRIES && err != ESP_OK; ++attempt) {
        err = i2c_master_transmit_receive(s_device, &first, 1, bytes, count, RTC_I2C_TIMEOUT_MS);
    }
    return err;
}

static esp_err_t write_registers(uint8_t first, const uint8_t *bytes, size_t count)
{
    uint8_t frame[1 + RTC_CHIP_TIME_REGS];
    if (count > RTC_CHIP_TIME_REGS) return ESP_ERR_INVALID_SIZE;
    frame[0] = first;
    for (size_t i = 0; i < count; ++i) frame[1 + i] = bytes[i];
    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < RTC_I2C_TRIES && err != ESP_OK; ++attempt) {
        err = i2c_master_transmit(s_device, frame, 1 + count, RTC_I2C_TIMEOUT_MS);
    }
    return err;
}

bool board_rtc_restore(void)
{
    if (!open_chip()) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint8_t regs[RTC_CHIP_TIME_REGS];
    esp_err_t err = read_registers(0x00, regs, sizeof(regs));
    uint8_t status = 0U;
    if (err == ESP_OK && s_chip == RTC_CHIP_DS3231) {
        err = read_registers(RTC_CHIP_DS3231_STATUS, &status, 1);
    }
    xSemaphoreGive(s_lock);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s does not answer: %s", chip_name(), esp_err_to_name(err));
        return false;
    }
    /* The DS3231 says when its oscillator stopped since this flag was last
     * cleared - a flat or missing cell with the board off - and the time it
     * holds is then wherever the count stopped, which may still decode. */
    if (s_chip == RTC_CHIP_DS3231 && (status & RTC_CHIP_DS3231_OSF) != 0U) {
        /* The control register too, for the one setting that makes the chip
         * stop on purpose when left on its cell. */
        uint8_t control = 0U;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        (void)read_registers(RTC_CHIP_DS3231_CONTROL, &control, 1);
        xSemaphoreGive(s_lock);
        ESP_LOGW(TAG, "DS3231 lost the time (oscillator stopped; status %02x, control %02x%s); "
                 "waiting for the network", status, control,
                 (control & RTC_CHIP_DS3231_EOSC) != 0U ? ", stops on its cell" : ", check the cell");
        return false;
    }
    struct tm utc;
    if (!rtc_chip_decode(s_chip, regs, &utc)) {
        ESP_LOGW(TAG, "%s holds no time (%02x %02x %02x %02x %02x %02x %02x); waiting for the network",
                 chip_name(), regs[0], regs[1], regs[2], regs[3], regs[4], regs[5], regs[6]);
        return false;
    }
    /* Whole seconds: the chip counts no finer. The clock is up to a second
     * behind until the first time-server answer, which nobody reading hours
     * and minutes will see. */
    const struct timeval now = {.tv_sec = (time_t)rtc_chip_epoch(&utc), .tv_usec = 0};
    settimeofday(&now, NULL);
    ESP_LOGI(TAG, "clock set from the %s: %04d-%02d-%02d %02d:%02d:%02d UTC", chip_name(),
             utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec);
    return true;
}

void board_rtc_store(void)
{
    if (!open_chip()) return;
    const time_t now = time(NULL);
    if (now < RTC_CLOCK_SET_AFTER) return;
    struct tm utc;
    gmtime_r(&now, &utc);
    uint8_t regs[RTC_CHIP_TIME_REGS];
    if (!rtc_chip_encode(s_chip, &utc, regs)) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = write_registers(0x00, regs, sizeof(regs));
    if (err == ESP_OK && s_chip == RTC_CHIP_DS3231) {
        /* The oscillator to run on the cell as well, should anything have
         * told it otherwise - a clock that stops whenever the board is off
         * keeps nothing for the next boot. */
        uint8_t control = 0U;
        err = read_registers(RTC_CHIP_DS3231_CONTROL, &control, 1);
        if (err == ESP_OK && (control & RTC_CHIP_DS3231_EOSC) != 0U) {
            control &= (uint8_t)~RTC_CHIP_DS3231_EOSC;
            err = write_registers(RTC_CHIP_DS3231_CONTROL, &control, 1);
            if (err == ESP_OK) ESP_LOGW(TAG, "DS3231 was set to stop on its cell; now it runs");
        }
        /* The oscillator-stop flag down, now that the time in the chip is good
         * again; everything else in the register as it was. */
        uint8_t status = 0U;
        if (err == ESP_OK) err = read_registers(RTC_CHIP_DS3231_STATUS, &status, 1);
        if (err == ESP_OK && (status & RTC_CHIP_DS3231_OSF) != 0U) {
            status &= (uint8_t)~RTC_CHIP_DS3231_OSF;
            err = write_registers(RTC_CHIP_DS3231_STATUS, &status, 1);
        }
    }
    xSemaphoreGive(s_lock);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s not written: %s", chip_name(), esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "%s set to %04d-%02d-%02d %02d:%02d:%02d UTC", chip_name(), utc.tm_year + 1900,
             utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec);
}
