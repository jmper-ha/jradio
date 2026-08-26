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

/* What the QR code behind that band encodes. False when there is nothing worth
 * encoding, which is also what decides whether the band offers the QR at all.
 *
 * Two payloads, because "reach the box" means two different things. On a joined
 * network the address is the whole answer and a phone camera opens it. In setup
 * mode the phone is not on the box's network yet, so a URL would fail before
 * the browser drew anything - there the QR is the join itself, in the WIFI:
 * form every phone camera understands. The setup AP is open, which is why the
 * payload says T:nopass and carries no password: this QR is on a screen anyone
 * in the room can photograph.
 *
 * Not language-dependent, unlike the band's text: neither payload is read by a
 * human. */
bool ui_web_address_qr(wifi_provisioning_mode_t mode, const char *ipv4, const char *ssid,
                       char *out, size_t out_size);
