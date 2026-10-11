#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

/* I2C0, shared: the FM tuner and the real-time clock both sit on it.
 *
 * It used to be the tuner's own, made in fm_tuner_init(); a second device is
 * what moved it here. Made on the first ask, from the wiring's i2c0_sda and
 * i2c0_scl, and never taken down - a device that is not there removes only
 * itself. The first ask comes from app_main, before any other task could make
 * a second one; after that the driver's own bus lock lets the devices take
 * turns. ESP_ERR_INVALID_STATE when the wiring names no pins. */
esp_err_t board_i2c0_bus(i2c_master_bus_handle_t *bus);
