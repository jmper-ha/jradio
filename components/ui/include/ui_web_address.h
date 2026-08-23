#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "wifi_provisioning.h"

/* Text of the settings screen's bottom band.
 *
 * This is the only place the device says where to reach its web UI. Before the
 * band existed the address lived in the UART log alone, so the web UI could
 * only be opened by someone who already knew the address.
 *
 * Setup mode gets an address too, and that is the case worth being careful
 * about: while the box is running its own access point, http://192.168.4.1 is
 * exactly what the user needs and exactly when they cannot look it up
 * anywhere else. Treating "connected" as "joined a network" hid it.
 *
 * `ssid` is the network the box is on, and in setup mode that is its own
 * access point - the one the user has to join before the address means
 * anything. It is shown there for that reason, and nowhere else: on a real
 * network the user already knows which one they are on.
 */
void ui_web_address_text(wifi_provisioning_mode_t mode, const char *ipv4, const char *ssid,
                         bool english, char *out, size_t out_size);
