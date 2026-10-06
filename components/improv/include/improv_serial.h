#pragma once

#include "esp_err.h"

/* Listens for Improv Wi-Fi on both serial ports a board can be plugged in by
 * - UART0, behind the USB bridge most boards carry, and the chip's own USB -
 * and answers on the one a packet came from. The web flasher uses it to hand
 * the device a network right after writing it, without the setup access
 * point; it works on any running board the same way.
 *
 * Once, after the Wi-Fi is started: it reads the device's name and version
 * here, where reading the flash is allowed, because the listening task keeps
 * its small stack in PSRAM and may not. */
esp_err_t improv_serial_start(void);
