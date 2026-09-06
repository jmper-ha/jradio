#include "dlna_discovery.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/sockets.h"

#include "dlna_client.h"

static const char *TAG = "dlna_discovery";

/* How long a server may wait before answering, in seconds. Servers spread
 * their replies over this window so a busy network does not deliver them all
 * at once, so listening for less than it means missing the slow ones. */
#define DLNA_DISCOVERY_MX 2U

/* The search goes out twice. SSDP is UDP with nothing to retransmit it, and a
 * single lost datagram is a server that silently is not there - which reads to
 * a user as "the device cannot see my NAS" rather than as a dropped packet.
 * The duplicate answers this provokes cost one comparison each. */
#define DLNA_DISCOVERY_SEARCH_SENDS 2U

/* A datagram larger than this is not an SSDP reply. The one measured here is
 * 400 bytes; the headers are few and short by design. */
#define DLNA_DISCOVERY_DATAGRAM_MAX 1024U

static bool already_seen(const dlna_server_t *servers, size_t count,
                         const dlna_ssdp_response_t *response)
{
    for (size_t index = 0U; index < count; ++index) {
        /* By identity where the server gave one, and by address otherwise -
         * a server that sends no USN is still only one server. */
        if (response->usn[0] != '\0' && servers[index].usn[0] != '\0') {
            if (strcmp(servers[index].usn, response->usn) == 0) return true;
        } else if (strcmp(servers[index].location, response->location) == 0) {
            return true;
        }
    }
    return false;
}

/* Collects the answers to one search. Returns how many distinct servers
 * announced themselves; their descriptions are not fetched yet, because doing
 * that here would stop the socket reading while the rest are answering. */
static size_t collect(dlna_server_t *servers, size_t capacity, uint32_t listen_ms)
{
    const int socket_handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_handle < 0) {
        ESP_LOGE(TAG, "no socket for a search");
        return 0U;
    }

    /* Short enough that the loop below notices its own deadline rather than
     * sitting in recvfrom past it. */
    const struct timeval receive_timeout = {.tv_sec = 0, .tv_usec = 250 * 1000};
    setsockopt(socket_handle, SOL_SOCKET, SO_RCVTIMEO, &receive_timeout,
               sizeof(receive_timeout));

    /* One hop past the local segment. A media server is on the same network as
     * the device; letting the search cross routers would only find servers it
     * then could not stream from. */
    const uint8_t ttl = 2U;
    setsockopt(socket_handle, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

    struct sockaddr_in group = {
        .sin_family = AF_INET,
        .sin_port = htons(DLNA_SSDP_PORT),
    };
    group.sin_addr.s_addr = inet_addr(DLNA_SSDP_MULTICAST_ADDRESS);

    char search[256];
    const size_t search_length = dlna_ssdp_build_search(search, sizeof(search),
                                                        DLNA_DISCOVERY_MX);
    if (search_length == 0U) {
        close(socket_handle);
        return 0U;
    }
    for (size_t attempt = 0U; attempt < DLNA_DISCOVERY_SEARCH_SENDS; ++attempt) {
        if (sendto(socket_handle, search, search_length, 0, (struct sockaddr *)&group,
                   sizeof(group)) < 0) {
            ESP_LOGW(TAG, "search %u of %u could not be sent", (unsigned)attempt + 1U,
                     (unsigned)DLNA_DISCOVERY_SEARCH_SENDS);
        }
    }

    size_t count = 0U;
    const int64_t deadline = esp_timer_get_time() + (int64_t)listen_ms * 1000;
    char datagram[DLNA_DISCOVERY_DATAGRAM_MAX];

    while (esp_timer_get_time() < deadline && count < capacity) {
        struct sockaddr_in from;
        socklen_t from_length = sizeof(from);
        const int received = recvfrom(socket_handle, datagram, sizeof(datagram) - 1U, 0,
                                      (struct sockaddr *)&from, &from_length);
        if (received <= 0) continue;
        datagram[received] = '\0';

        dlna_ssdp_response_t response;
        if (!dlna_ssdp_parse_response(datagram, (size_t)received, &response)) continue;
        if (already_seen(servers, count, &response)) continue;

        memset(&servers[count], 0, sizeof(servers[count]));
        snprintf(servers[count].usn, sizeof(servers[count].usn), "%s", response.usn);
        snprintf(servers[count].location, sizeof(servers[count].location), "%s",
                 response.location);
        ++count;
    }

    close(socket_handle);
    return count;
}

/* A name for a server that gave none: the address it answers on, which is at
 * least something a person can recognise on a list. Its host and port rather
 * than the whole location, because the path is the same "/DeviceDescription.xml"
 * on every one of them and a name cut off in the middle of a path tells the
 * reader nothing. */
static void name_by_address(const char *location, char *out, size_t out_size)
{
    out[0] = '\0';
    const char *host = strstr(location, "://");
    host = host != NULL ? host + 3 : location;
    const char *end = strchr(host, '/');
    const size_t length = end != NULL ? (size_t)(end - host) : strlen(host);
    const size_t kept = length + 1U <= out_size ? length : out_size - 1U;
    memcpy(out, host, kept);
    out[kept] = '\0';
}

size_t dlna_discovery_search(dlna_server_t *servers, size_t capacity, uint32_t listen_ms)
{
    if (servers == NULL || capacity == 0U) return 0U;
    if (capacity > DLNA_DISCOVERY_SERVER_MAX) capacity = DLNA_DISCOVERY_SERVER_MAX;

    const size_t announced = collect(servers, capacity, listen_ms);
    if (announced == 0U) {
        ESP_LOGI(TAG, "no media server answered in %u ms", (unsigned)listen_ms);
        return 0U;
    }

    if (dlna_client_open() != ESP_OK) return 0U;

    /* Compacted in place: a server that answered and offers nothing to browse
     * is not a row, and leaving a hole would make the count a lie. */
    size_t kept = 0U;
    for (size_t index = 0U; index < announced; ++index) {
        dlna_device_t device;
        if (dlna_client_fetch_description(servers[index].location, &device) != ESP_OK) {
            continue;
        }
        if (kept != index) servers[kept] = servers[index];
        servers[kept].device = device;
        if (servers[kept].device.friendly_name[0] == '\0') {
            name_by_address(servers[kept].location, servers[kept].device.friendly_name,
                            sizeof(servers[kept].device.friendly_name));
        }
        ++kept;
    }

    ESP_LOGI(TAG, "%u of %u servers can be browsed", (unsigned)kept, (unsigned)announced);
    return kept;
}
