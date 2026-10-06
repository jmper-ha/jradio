#include "improv_serial.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "device_settings.h"
#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hal/usb_serial_jtag_ll.h"
#include "improv_proto.h"
#include "version_info.h"
#include "wifi_provisioning.h"

static const char *TAG = "improv";

/* The listener only reads, parses and answers, so its stack can sit in PSRAM
 * and cost the internal SRAM nothing. Saving a network and scanning go to a
 * worker that lives for one command: those reach the Wi-Fi driver, which
 * wants a stack in internal RAM, and internal RAM is what this firmware is
 * shortest of - a 4 KB stack held for a command used once is better borrowed
 * than kept. */
#define IMPROV_LISTEN_STACK 3072U
#define IMPROV_WORKER_STACK 4096U
#define IMPROV_POLL_MS 20U
#define IMPROV_UART UART_NUM_0
#define IMPROV_UART_RX_RING 256
/* How long a new network gets to hand out an address before the answer is
 * "unable to connect". The provisioning code gives up on its own well before
 * this and rolls back; this only stops the flasher from waiting forever. */
#define IMPROV_CONNECT_WAIT_MS 40000U
#define IMPROV_SCAN_MAX 16U

typedef enum {
    IMPROV_PORT_UART = 0,
    IMPROV_PORT_USB,
    IMPROV_PORT_COUNT,
} improv_port_t;

static improv_decoder_t s_decoders[IMPROV_PORT_COUNT];
static bool s_uart_ready;
static SemaphoreHandle_t s_write_lock;
static atomic_bool s_worker_busy;
static char s_version[VERSION_INFO_STRING_MAX + 1U];
static char s_name[DEVICE_NAME_MAX];

typedef struct {
    improv_port_t port;
    improv_rpc_t rpc;
} improv_job_t;
static improv_job_t s_job;

static void secure_zero(void *buffer, size_t length)
{
    volatile unsigned char *bytes = buffer;
    while (length-- > 0U) *bytes++ = 0U;
}

/* One frame, whole: the log shares these lines, and a frame cut in two by a
 * log line is one the client throws away. The lock keeps this task and the
 * worker from interleaving; the log itself cannot be held off, and a frame it
 * splits is asked for again. */
static void improv_write(improv_port_t port, const uint8_t *bytes, size_t length)
{
    if (length == 0U) return;
    xSemaphoreTake(s_write_lock, portMAX_DELAY);
    if (port == IMPROV_PORT_UART) {
        if (s_uart_ready) (void)uart_write_bytes(IMPROV_UART, bytes, length);
    } else {
        /* Nobody listening on the USB side means a FIFO that never drains;
         * give up after a moment rather than hold the lock. */
        TickType_t started = xTaskGetTickCount();
        size_t written = 0U;
        while (written < length && xTaskGetTickCount() - started < pdMS_TO_TICKS(100)) {
            if (usb_serial_jtag_ll_txfifo_writable()) {
                written += usb_serial_jtag_ll_write_txfifo(bytes + written,
                                                           (uint32_t)(length - written));
                usb_serial_jtag_ll_txfifo_flush();
            } else {
                vTaskDelay(1);
            }
        }
    }
    xSemaphoreGive(s_write_lock);
}

static void improv_send_state(improv_port_t port, improv_state_t state)
{
    uint8_t frame[IMPROV_FRAME_MAX];
    improv_write(port, frame, improv_frame_state(state, frame, sizeof(frame)));
}

static void improv_send_error(improv_port_t port, improv_error_t error)
{
    uint8_t frame[IMPROV_FRAME_MAX];
    improv_write(port, frame, improv_frame_error(error, frame, sizeof(frame)));
}

static void improv_send_result(improv_port_t port, improv_command_t command,
                               const char *const *strings, size_t count)
{
    uint8_t frame[IMPROV_FRAME_MAX];
    improv_write(port, frame, improv_frame_result(command, strings, count, frame, sizeof(frame)));
}

/* Provisioned is having an address: that is when the flasher has somewhere to
 * send the browser. */
static bool improv_connected(char *url, size_t url_size)
{
    const wifi_provisioning_status_t status = wifi_provisioning_status();
    if (status.ipv4[0] == '\0') return false;
    snprintf(url, url_size, "http://%s", status.ipv4);
    return true;
}

static void improv_send_current(improv_port_t port, improv_command_t answering)
{
    char url[32];
    if (improv_connected(url, sizeof(url))) {
        improv_send_state(port, IMPROV_STATE_PROVISIONED);
        const char *strings[] = {url};
        improv_send_result(port, answering, strings, 1U);
    } else {
        improv_send_state(port, IMPROV_STATE_READY);
    }
}

static void improv_join(improv_port_t port, improv_rpc_t *rpc)
{
    improv_send_error(port, IMPROV_ERROR_NONE);
    improv_send_state(port, IMPROV_STATE_PROVISIONING);
    /* Through the same door the web page uses: the network is tried and only
     * written to wifi.json once it has given an address, and a network that
     * never does is rolled back to the ones that worked. */
    const esp_err_t err = wifi_provisioning_save_network(rpc->ssid, rpc->password);
    secure_zero(rpc->password, sizeof(rpc->password));
    ESP_LOGI(TAG, "network \"%s\" received over serial: %s", rpc->ssid, esp_err_to_name(err));
    bool joined = false;
    if (err == ESP_OK) {
        const TickType_t started = xTaskGetTickCount();
        while (xTaskGetTickCount() - started < pdMS_TO_TICKS(IMPROV_CONNECT_WAIT_MS)) {
            vTaskDelay(pdMS_TO_TICKS(250));
            const wifi_provisioning_status_t status = wifi_provisioning_status();
            if (status.save_pending) continue;
            joined = status.ipv4[0] != '\0' && strcmp(status.active_ssid, rpc->ssid) == 0;
            break;
        }
    }
    if (joined) {
        char url[32];
        if (improv_connected(url, sizeof(url))) {
            improv_send_state(port, IMPROV_STATE_PROVISIONED);
            const char *strings[] = {url};
            improv_send_result(port, IMPROV_COMMAND_WIFI_SETTINGS, strings, 1U);
            return;
        }
    }
    improv_send_error(port, IMPROV_ERROR_UNABLE_TO_CONNECT);
    char url[32];
    improv_send_state(port, improv_connected(url, sizeof(url)) ? IMPROV_STATE_PROVISIONED
                                                               : IMPROV_STATE_READY);
}

static void improv_list_networks(improv_port_t port)
{
    wifi_provisioning_scan_entry_t entries[IMPROV_SCAN_MAX];
    uint8_t count = 0U;
    /* Refused while the device is on a network - scanning hops channels and
     * would break the stream - so a running radio answers with an empty list
     * and the flasher asks for the name to be typed. A freshly flashed one is
     * in its setup mode, where the scan runs. */
    esp_err_t err = wifi_provisioning_scan_start();
    if (err == ESP_OK) {
        do {
            vTaskDelay(pdMS_TO_TICKS(250));
            err = wifi_provisioning_scan_result(entries, IMPROV_SCAN_MAX, &count);
        } while (err == ESP_ERR_NOT_FINISHED);
    }
    if (err != ESP_OK) count = 0U;
    for (uint8_t index = 0U; index < count; ++index) {
        if (entries[index].ssid[0] == '\0') continue;
        char rssi[8];
        snprintf(rssi, sizeof(rssi), "%d", (int)entries[index].rssi);
        const char *strings[] = {entries[index].ssid, rssi, entries[index].secure ? "YES" : "NO"};
        improv_send_result(port, IMPROV_COMMAND_GET_NETWORKS, strings, 3U);
    }
    improv_send_result(port, IMPROV_COMMAND_GET_NETWORKS, NULL, 0U);
}

static void improv_worker(void *arg)
{
    improv_job_t *job = arg;
    if (job->rpc.command == IMPROV_COMMAND_WIFI_SETTINGS) {
        improv_join(job->port, &job->rpc);
    } else {
        improv_list_networks(job->port);
    }
    secure_zero(&job->rpc, sizeof(job->rpc));
    atomic_store(&s_worker_busy, false);
    vTaskDelete(NULL);
}

static void improv_handle(improv_port_t port, improv_decoder_t *decoder)
{
    if (decoder->type != IMPROV_TYPE_RPC) return;
    improv_rpc_t rpc;
    const improv_error_t parsed = improv_rpc_parse(decoder->data, decoder->length, &rpc);
    // The data field may have held the password; it is in `rpc` now.
    improv_decoder_reset(decoder);
    if (parsed != IMPROV_ERROR_NONE) {
        improv_send_error(port, parsed);
        return;
    }
    switch (rpc.command) {
    case IMPROV_COMMAND_GET_STATE:
        if (atomic_load(&s_worker_busy)) improv_send_state(port, IMPROV_STATE_PROVISIONING);
        else improv_send_current(port, IMPROV_COMMAND_GET_STATE);
        break;
    case IMPROV_COMMAND_GET_INFO: {
        const char *strings[] = {"jRadio", s_version, "ESP32-S3", s_name};
        improv_send_result(port, IMPROV_COMMAND_GET_INFO, strings, 4U);
        break;
    }
    case IMPROV_COMMAND_WIFI_SETTINGS:
    case IMPROV_COMMAND_GET_NETWORKS: {
        bool idle = false;
        if (!atomic_compare_exchange_strong(&s_worker_busy, &idle, true)) {
            improv_send_error(port, IMPROV_ERROR_UNKNOWN);
            break;
        }
        s_job.port = port;
        s_job.rpc = rpc;
        if (xTaskCreate(improv_worker, "improv_work", IMPROV_WORKER_STACK, &s_job, 3, NULL) !=
            pdPASS) {
            ESP_LOGW(TAG, "no room for the worker");
            secure_zero(&s_job.rpc, sizeof(s_job.rpc));
            atomic_store(&s_worker_busy, false);
            improv_send_error(port, IMPROV_ERROR_UNKNOWN);
        }
        break;
    }
    default:
        improv_send_error(port, IMPROV_ERROR_UNKNOWN_RPC);
        break;
    }
    secure_zero(&rpc, sizeof(rpc));
}

static size_t improv_read(improv_port_t port, uint8_t *buffer, size_t capacity)
{
    if (port == IMPROV_PORT_UART) {
        if (!s_uart_ready) return 0U;
        const int got = uart_read_bytes(IMPROV_UART, buffer, (uint32_t)capacity, 0);
        return got > 0 ? (size_t)got : 0U;
    }
    /* The USB side has no driver - the log goes out through it as a second
     * console, which never reads - so the FIFO is read directly. A host that
     * sends while nobody reads is held off by USB itself; nothing is lost. */
    size_t got = 0U;
    while (got < capacity && usb_serial_jtag_ll_rxfifo_data_available()) {
        got += usb_serial_jtag_ll_read_rxfifo(buffer + got, (uint32_t)(capacity - got));
    }
    return got;
}

static void improv_listen(void *arg)
{
    (void)arg;
    uint8_t buffer[64];
    while (true) {
        for (int port = 0; port < IMPROV_PORT_COUNT; ++port) {
            const size_t got = improv_read((improv_port_t)port, buffer, sizeof(buffer));
            for (size_t index = 0U; index < got; ++index) {
                if (improv_decoder_feed(&s_decoders[port], buffer[index])) {
                    improv_handle((improv_port_t)port, &s_decoders[port]);
                }
            }
            secure_zero(buffer, got);
        }
        vTaskDelay(pdMS_TO_TICKS(IMPROV_POLL_MS));
    }
}

esp_err_t improv_serial_start(void)
{
    if (s_write_lock != NULL) return ESP_OK;
    s_write_lock = xSemaphoreCreateMutex();
    if (s_write_lock == NULL) return ESP_ERR_NO_MEM;

    version_info_t version;
    version_info_read(&version);
    snprintf(s_version, sizeof(s_version), "%s", version.firmware.version);
    device_settings_t settings;
    if (device_settings_init(&settings) && settings.device_name[0] != '\0') {
        snprintf(s_name, sizeof(s_name), "%s", settings.device_name);
    } else {
        unsigned char mac[6] = {0};
        (void)esp_read_mac(mac, ESP_MAC_WIFI_STA);
        device_settings_default_name(mac, s_name, sizeof(s_name));
    }

    /* Receive only: the log keeps writing to the FIFO the way it always has,
     * and this side's frames go out through the driver's direct write. */
    const esp_err_t uart = uart_driver_install(IMPROV_UART, IMPROV_UART_RX_RING, 0, 0, NULL, 0);
    s_uart_ready = uart == ESP_OK;
    if (!s_uart_ready) ESP_LOGW(TAG, "UART0 not listened to: %s", esp_err_to_name(uart));
    for (int port = 0; port < IMPROV_PORT_COUNT; ++port) improv_decoder_reset(&s_decoders[port]);

    if (xTaskCreatePinnedToCoreWithCaps(improv_listen, "improv", IMPROV_LISTEN_STACK, NULL, 2,
                                        NULL, tskNO_AFFINITY,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "listening on UART0%s and USB", s_uart_ready ? "" : " (not)");
    return ESP_OK;
}
