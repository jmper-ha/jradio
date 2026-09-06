#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "dlna_device.h"
#include "dlna_didl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Talking to a media server: the two requests, over HTTP.
 *
 * Plain HTTP, and that is worth saying out loud. A media server on the LAN
 * speaks it unencrypted, so none of this needs TLS - and TLS is exactly what
 * makes the internet radio expensive here, because mbedTLS's AES wants DMA-
 * capable memory and internal SRAM is the scarce thing on this board. A DLNA
 * server costs a socket and a buffer.
 *
 * The buffer is the expensive part and it is not held: a page of a listing is
 * some 22 KB of XML, which is PSRAM either way, but there is no reason for it
 * to sit allocated while the device is playing the radio. It is taken when the
 * source is opened and given back when it is closed, the same discipline the
 * SD card's mount follows and for the same reason. */

/* Takes the response buffer. Idempotent, so every entry point into the source
 * can call it. */
esp_err_t dlna_client_open(void);

/* Gives it back. Safe to call when nothing was open. */
void dlna_client_close(void);

/* Fetches and reads a device description found by a search. */
esp_err_t dlna_client_fetch_description(const char *location, dlna_device_t *out);

typedef struct {
    /* Rows written into the caller's array. */
    size_t count;
    /* How many the container holds altogether, which is how the caller knows
     * whether there is another page behind this one. */
    size_t total_matches;
} dlna_client_page_t;

/* Asks a server for the children of one object.
 *
 * `starting_index` walks a container larger than one page and `requested_count`
 * says how much of it to ask for. Entries the parser could not use are absent
 * from `count` but still counted by the server's total, so a caller paging to
 * the end compares against `starting_index + requested_count` rather than
 * against what it received.
 *
 * Returns ESP_ERR_INVALID_SIZE when the answer filled the response buffer.
 * That is not a broken server: nothing bounds how much a server says about an
 * object, and one artist container here needs 3.7 KB where a track needs 1.2.
 * The caller's move is to ask for fewer entries rather than to give up - which
 * is why the count is a parameter and not the constant it started as. */
esp_err_t dlna_client_browse(const char *control_url, const char *object_id,
                             size_t starting_index, size_t requested_count,
                             dlna_entry_t *entries, size_t capacity,
                             dlna_client_page_t *page);

/* Fetches a picture into the caller's buffer - the cover of a track.
 *
 * Its own call rather than a browse because the buffer is the caller's: a
 * cover is wanted for one track at a time and is not worth a permanent
 * allocation. ESP_ERR_INVALID_SIZE when the picture is larger than the room
 * offered, which is refused whole: half a JPEG decodes to nothing, and an
 * empty tile beats a wrong one. */
esp_err_t dlna_client_fetch_image(const char *url, uint8_t *buffer, size_t capacity,
                                  size_t *length);

#ifdef __cplusplus
}
#endif
