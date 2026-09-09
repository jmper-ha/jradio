#include "dlna_source.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "album_art.h"
#include "dlna_browse_stack.h"
#include "dlna_client.h"
#include "dlna_root_filter.h"
#include "dlna_soap.h"
#include "dlna_ssdp.h"
#include "device_settings.h"
#include "device_text.h"
#include "internet_radio.h"

static const char *TAG = "dlna_source";

/* How long to listen for servers. Two seconds is the window the search itself
 * asks them to answer within; a little more covers a server that answered at
 * the far edge of it. */
#define DLNA_SOURCE_LISTEN_MS 2500U

/* Room for one cover while it is being fetched, taken and given back around
 * the fetch rather than held: it is wanted for a moment at the start of a
 * track. Measured against the server here - 21 KB for the small picture, 70 KB
 * for the large one a server that offers only upnp:albumArtURI would send - so
 * this covers both with margin. */
#define DLNA_SOURCE_COVER_MAX (96U * 1024U)

static SemaphoreHandle_t s_lock;
static dlna_server_t s_servers[DLNA_DISCOVERY_SERVER_MAX];
static size_t s_server_count;
static size_t s_selected;

static dlna_browse_stack_t s_stack;
static dlna_entry_t *s_entries;
/* The listing being read, which the browse writes into while holding no lock,
 * and which is swapped with the one above in a few instructions once it is
 * complete. Two buffers rather than one because a browse is several HTTP round
 * trips and everything that draws the browser polls the listing: with the lock
 * held across the requests, the UI task and the web server's single worker
 * both stopped dead on it. Measured: one container that would not open blocked
 * /api/dlna for 10.2 seconds and froze the panel for the same. */
static dlna_entry_t *s_staging;
static size_t s_entry_count;
static bool s_open;
/* The browser is on the list of servers rather than inside one of them.
 *
 * A level above every server's own root, and it exists only when more than one
 * answered the search. With one - which is what a home network usually has -
 * there is nothing to choose between, and an extra screen to walk through
 * every time would be a step charged to everybody to serve the rare case. */
static bool s_at_server_list;
/* Set for the whole of dlna_source_open(), which blocks for a second or two.
 * Everything that reads the listing meanwhile needs to know the difference
 * between "not yet" and "not at all". */
static bool s_searching;

/* The row being played, and the URL and title handed to the audio path.
 *
 * The two buffers are here rather than on a stack because the audio path keeps
 * the pointer it is given until it asks for the next one - that is the
 * contract of internet_radio_track_source_fn. */
static size_t s_playing = DLNA_SOURCE_ENTRY_MAX;
static char s_playing_url[DLNA_URL_MAX];
static char s_playing_title[DLNA_TITLE_MAX];

/* Where a previous run left off, handed over before the source is opened and
 * spent by that open. See dlna_source_set_resume(). */
static bool s_resume_wanted;
static char s_resume_server[DLNA_SSDP_UDN_MAX];
static char s_resume_container[DLNA_OBJECT_ID_MAX];
static char s_resume_title[DLNA_TITLE_MAX];
static char s_resume_track[DLNA_OBJECT_ID_MAX];

/* settings.csv is where a resume point is kept between runs, and it sizes its
 * fields without including these headers. This is where the two meet. */
_Static_assert(DEVICE_LAST_DLNA_SERVER_MAX >= DLNA_SSDP_UDN_MAX,
               "a server's uuid must fit the settings field");
_Static_assert(DEVICE_LAST_DLNA_ID_MAX >= DLNA_OBJECT_ID_MAX,
               "an object id must fit the settings field");
_Static_assert(DEVICE_LAST_DLNA_TITLE_MAX >= DLNA_TITLE_MAX,
               "a container title must fit the settings field");

static void lock(void)
{
    if (s_lock != NULL) xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock(void)
{
    if (s_lock != NULL) xSemaphoreGive(s_lock);
}

static void set_searching(bool searching)
{
    lock();
    s_searching = searching;
    unlock();
}

/* Drops the root's non-audio sections. Called with the lock held, and only at
 * the root - see dlna_root_filter.h for why the name is all there is to go on.
 *
 * A filter that emptied the listing would be indistinguishable from a server
 * that answered nothing, and the user would have no way to tell which had
 * happened or to get past it. So an empty result is refused: the rows go back
 * exactly as the server sent them, and the guess is written off rather than
 * acted on. */
static size_t keep_music_sections(dlna_entry_t *entries, size_t count)
{
    size_t kept = 0U;
    for (size_t index = 0U; index < count; ++index) {
        if (entries[index].kind == DLNA_ENTRY_CONTAINER &&
            !dlna_root_filter_is_music(entries[index].title)) {
            continue;
        }
        if (kept != index) entries[kept] = entries[index];
        ++kept;
    }
    if (kept == 0U) {
        ESP_LOGI(TAG, "no section of the root looks like music; showing all %u",
                 (unsigned)count);
        return count;
    }
    if (kept < count) {
        ESP_LOGI(TAG, "root: %u of %u sections kept", (unsigned)kept, (unsigned)count);
    }
    return kept;
}

/* Reads the container the stack is pointing at, a page at a time, until the
 * listing is full or the server has no more to give.
 *
 * Called **without** the lock, and only from the player task, which is the one
 * thing that ever writes a listing. The lock is taken twice and briefly: once
 * to copy out where to read from, once to publish what came back. In between
 * are the HTTP round trips, and they are why: everything that draws the
 * browser polls this listing, so a lock held across the network is a frozen
 * panel and a web server that answers nothing.
 *
 * A failed read publishes nothing at all. The previous container's rows stay
 * on screen, which is where the user still is - the stack is only committed by
 * the caller once this has succeeded. */
static esp_err_t read_current_container(void)
{
    char control_url[DLNA_URL_MAX];
    char object_id[DLNA_OBJECT_ID_MAX + 1U];
    bool at_root;

    lock();
    if (!s_open || s_staging == NULL) {
        unlock();
        return ESP_ERR_INVALID_STATE;
    }
    snprintf(control_url, sizeof(control_url), "%s",
             s_servers[s_selected].device.control_url);
    snprintf(object_id, sizeof(object_id), "%s", dlna_browse_stack_id(&s_stack));
    at_root = dlna_browse_stack_at_root(&s_stack);
    unlock();

    size_t count = 0U;

    /* How much to ask for at a time. It starts at the measured page and comes
     * down when a server answers with more than the buffer holds - see
     * dlna_client_browse(). Kept across pages of the same container because a
     * container's rows are all of a kind: if the first eight artists did not
     * fit, the next eight will not either. */
    size_t page_size = DLNA_SOAP_BROWSE_PAGE;
    size_t starting_index = 0U;

    for (;;) {
        dlna_client_page_t page;
        const esp_err_t err = dlna_client_browse(control_url, object_id, starting_index,
                                                 page_size, s_staging + count,
                                                 DLNA_SOURCE_ENTRY_MAX - count, &page);
        if (err == ESP_ERR_INVALID_SIZE) {
            /* Ask again for half as much, from the same place. What has been
             * read so far is kept - it is complete and correct, the answer was
             * only cut after it. One entry that will not fit on its own is a
             * container this device genuinely cannot read. */
            if (page_size <= 1U) {
                ESP_LOGW(TAG, "'%s' says more about one entry than fits in a browse",
                         object_id);
                return err;
            }
            page_size /= 2U;
            continue;
        }
        if (err != ESP_OK) return err;

        count += page.count;
        /* Advanced by what was *asked for* rather than by what came back: rows
         * the parser could not use still occupy the server's numbering, and
         * counting only the kept ones would ask for the same page for ever. */
        starting_index += page_size;

        if (starting_index >= page.total_matches) break;
        if (count >= DLNA_SOURCE_ENTRY_MAX) {
            ESP_LOGW(TAG, "'%s' holds %u entries; showing the first %u", object_id,
                     (unsigned)page.total_matches, (unsigned)count);
            break;
        }
        if (page.count == 0U) {
            /* A page that added nothing at all, with more supposedly to come.
             * Believing the total here is how a browse loops for ever. */
            break;
        }
    }
    /* The root of a server is its sections - video, music, pictures - and this
     * device plays one of the three. The rest are rows that can only be walked
     * into and come back from empty-handed. */
    if (at_root) count = keep_music_sections(s_staging, count);

    /* Published by swapping the pointers, so the listing on screen changes
     * between two instructions rather than being rewritten row by row under
     * the eyes of whoever is reading it. */
    lock();
    dlna_entry_t *const previous = s_entries;
    s_entries = s_staging;
    s_staging = previous;
    s_entry_count = count;
    s_playing = DLNA_SOURCE_ENTRY_MAX;
    unlock();
    return ESP_OK;
}

/* The next track of the chain, and the one on the air again after a pause.
 *
 * These are what internet_radio calls; they are the seam that lets the audio
 * path play a chain of finite HTTP tracks without knowing that a media server
 * is behind it - the same seam the Yandex rotor hangs on. */
/* Fetches and shows the cover the server named for a track.
 *
 * Called with no lock held and on the audio path's own task, between one track
 * ending and the next being opened - the same moment the rotor decodes its
 * picture. The buffer is taken and given back around the fetch: a cover is
 * wanted for an instant, and 96 KB held for the life of the source would be
 * 96 KB the decoder cannot have. */
static void dlna_publish_cover(const char *url)
{
    if (url == NULL || url[0] == '\0') {
        /* The track has no picture. Clearing rather than leaving the last one
         * up: a cover that outlives what it describes is worse than none. */
        album_art_clear();
        return;
    }

    uint8_t *buffer = heap_caps_malloc(DLNA_SOURCE_COVER_MAX, MALLOC_CAP_SPIRAM);
    if (buffer == NULL) buffer = heap_caps_malloc(DLNA_SOURCE_COVER_MAX, MALLOC_CAP_INTERNAL);
    if (buffer == NULL) {
        ESP_LOGD(TAG, "no room for a cover just now");
        return;
    }

    size_t length = 0U;
    const esp_err_t err = dlna_client_fetch_image(url, buffer, DLNA_SOURCE_COVER_MAX,
                                                  &length);
    if (err != ESP_OK || length == 0U) {
        ESP_LOGI(TAG, "cover not fetched: %s", esp_err_to_name(err));
        album_art_clear();
    } else if (!album_art_set_image(buffer, length)) {
        /* Identical bytes cost nothing - album_art keeps a checksum - so an
         * album whose tracks share one picture does not blank the tile on
         * every change. */
        ESP_LOGW(TAG, "cover did not decode (%u bytes)", (unsigned)length);
    }
    free(buffer);
}

static const char *dlna_next_url(char *title, size_t title_size)
{
    char art[DLNA_URL_MAX];

    lock();
    size_t index = s_playing == DLNA_SOURCE_ENTRY_MAX ? 0U : s_playing + 1U;
    while (index < s_entry_count && !s_entries[index].playable) ++index;
    if (index >= s_entry_count) {
        /* The end of the listing is the end of the chain. Not an error: it is
         * an album that finished. */
        s_playing = DLNA_SOURCE_ENTRY_MAX;
        unlock();
        return NULL;
    }

    s_playing = index;
    snprintf(s_playing_url, sizeof(s_playing_url), "%s", s_entries[index].url);
    /* The performer where the server gave one, because a row of bare track
     * names tells a listener nothing about what is playing. */
    if (title != NULL && title_size > 0U) {
        if (s_entries[index].artist[0] != '\0') {
            snprintf(title, title_size, "%s - %s", s_entries[index].artist,
                     s_entries[index].title);
        } else {
            snprintf(title, title_size, "%s", s_entries[index].title);
        }
    }
    snprintf(s_playing_title, sizeof(s_playing_title), "%s", s_entries[index].title);
    snprintf(art, sizeof(art), "%s", s_entries[index].art_url);
    unlock();

    /* Outside the lock on purpose: this is an HTTP round trip, and the browser
     * screen polls the listing every ten milliseconds. Holding the lock across
     * it would stall the panel for as long as the picture takes. */
    dlna_publish_cover(art);
    return s_playing_url;
}

static const char *dlna_current_url(void)
{
    lock();
    const bool playing = s_playing < s_entry_count;
    unlock();
    return playing ? s_playing_url : NULL;
}

static dlna_entry_t *alloc_listing(void)
{
    dlna_entry_t *listing = heap_caps_malloc(DLNA_SOURCE_ENTRY_MAX * sizeof(*listing),
                                             MALLOC_CAP_SPIRAM);
    if (listing == NULL) {
        listing = heap_caps_malloc(DLNA_SOURCE_ENTRY_MAX * sizeof(*listing),
                                   MALLOC_CAP_INTERNAL);
    }
    return listing;
}

/* Builds the listing out of the servers that answered.
 *
 * They are rows like any other container: the browser, the panel and the page
 * all draw them without knowing they are not folders, and dlna_source_activate
 * is the one place that has to tell the difference. Called with the lock
 * held. */
static void fill_server_list(void)
{
    s_entry_count = 0U;
    for (size_t index = 0U; index < s_server_count && index < DLNA_SOURCE_ENTRY_MAX;
         ++index) {
        dlna_entry_t *const row = &s_entries[s_entry_count];
        memset(row, 0, sizeof(*row));
        row->kind = DLNA_ENTRY_CONTAINER;
        snprintf(row->title, sizeof(row->title), "%s",
                 s_servers[index].device.friendly_name);
        ++s_entry_count;
    }
    s_playing = DLNA_SOURCE_ENTRY_MAX;
}

static bool apply_resume(void);

esp_err_t dlna_source_open(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) return ESP_ERR_NO_MEM;
    }

    set_searching(true);

    esp_err_t err = dlna_client_open();
    if (err != ESP_OK) {
        set_searching(false);
        return err;
    }

    lock();
    /* Two of them, the one on screen and the one being read - see
     * read_current_container(). PSRAM first, as everything of this size is. */
    if (s_entries == NULL) s_entries = alloc_listing();
    if (s_staging == NULL) s_staging = alloc_listing();
    if (s_entries == NULL || s_staging == NULL) {
        s_searching = false;
        unlock();
        ESP_LOGE(TAG, "no memory for two listings of %u entries",
                 (unsigned)DLNA_SOURCE_ENTRY_MAX);
        dlna_client_close();
        return ESP_ERR_NO_MEM;
    }
    unlock();

    /* Searched every time the source is opened rather than remembered. There
     * is no way for a server to tell this device it has appeared or gone, and
     * a remembered address that no longer answers costs a browse that hangs
     * for its whole timeout before saying so. */
    dlna_server_t found[DLNA_DISCOVERY_SERVER_MAX];
    const size_t count = dlna_discovery_search(found, DLNA_DISCOVERY_SERVER_MAX,
                                               DLNA_SOURCE_LISTEN_MS);

    lock();
    memcpy(s_servers, found, sizeof(s_servers));
    s_server_count = count;
    s_selected = 0U;
    s_entry_count = 0U;
    s_playing = DLNA_SOURCE_ENTRY_MAX;
    s_open = count > 0U;
    /* Spent here whatever comes of it: it says where *this* open should land,
     * and the next one - a user choosing the source by hand - belongs at the
     * top of the tree. */
    const bool resume = s_resume_wanted;
    s_resume_wanted = false;
    if (!s_open) {
        s_searching = false;
        unlock();
        return ESP_ERR_NOT_FOUND;
    }
    unlock();

    if (resume && apply_resume()) {
        set_searching(false);
        return ESP_OK;
    }

    lock();
    if (count > 1U) {
        /* More than one answered, and which of them "the first" is comes down
         * to which replied fastest - not a choice anybody made, and not the
         * same one twice. So nothing is opened: the browser starts on the list
         * of servers and the user says which. */
        s_at_server_list = true;
        fill_server_list();
        s_searching = false;
        unlock();
        ESP_LOGI(TAG, "%u servers answered; showing the list", (unsigned)count);
        return ESP_OK;
    }
    s_at_server_list = false;
    dlna_browse_stack_reset(&s_stack, s_servers[0].device.friendly_name);
    unlock();

    err = read_current_container();
    set_searching(false);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "open on '%s' with %u entries at the root",
                 s_servers[0].device.friendly_name, (unsigned)s_entry_count);
    }
    return err;
}

void dlna_source_close(void)
{
    lock();
    s_open = false;
    s_searching = false;
    s_entry_count = 0U;
    s_server_count = 0U;
    s_playing = DLNA_SOURCE_ENTRY_MAX;
    s_at_server_list = false;
    free(s_entries);
    s_entries = NULL;
    free(s_staging);
    s_staging = NULL;
    unlock();
    dlna_client_close();
}

bool dlna_source_is_open(void)
{
    lock();
    const bool open = s_open;
    unlock();
    return open;
}

bool dlna_source_is_searching(void)
{
    lock();
    const bool searching = s_searching;
    unlock();
    return searching;
}

size_t dlna_source_server_count(void)
{
    lock();
    const size_t count = s_server_count;
    unlock();
    return count;
}

bool dlna_source_server_name(size_t index, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0U) return false;
    out[0] = '\0';
    lock();
    const bool known = index < s_server_count;
    if (known) snprintf(out, out_size, "%s", s_servers[index].device.friendly_name);
    unlock();
    return known;
}

size_t dlna_source_selected_server(void)
{
    lock();
    const size_t selected = s_selected;
    unlock();
    return selected;
}

esp_err_t dlna_source_select_server(size_t index)
{
    lock();
    if (index >= s_server_count) {
        unlock();
        return ESP_ERR_INVALID_ARG;
    }
    s_selected = index;
    s_at_server_list = false;
    /* At the root, because a position in one server's tree names nothing in
     * another's - the ids are the server's own. */
    dlna_browse_stack_reset(&s_stack, s_servers[index].device.friendly_name);
    unlock();

    // Outside the lock, like every other read: it is several HTTP round trips,
    // and the panel and the web server both poll the listing.
    return read_current_container();
}

size_t dlna_source_entry_count(void)
{
    lock();
    const size_t count = s_entry_count;
    unlock();
    return count;
}

bool dlna_source_entry_at(size_t index, dlna_entry_t *out)
{
    if (out == NULL) return false;
    lock();
    const bool known = index < s_entry_count;
    /* Copied out under the lock: the listing can be replaced by a browse on
     * another task while this one is walking it. */
    if (known) *out = s_entries[index];
    unlock();
    return known;
}

const char *dlna_source_heading(void)
{
    /* Not copied: the stack's text only changes under the lock, and every
     * caller reads it to draw it immediately. The server list has no place in
     * the stack - it is above it - so it names itself. */
    return s_at_server_list
               ? device_text(DEVICE_TEXT_DLNA_SERVER_LIST, device_settings_published_language())
               : dlna_browse_stack_title(&s_stack);
}

bool dlna_source_at_root(void)
{
    lock();
    /* Whether there is anywhere above, which is what draws the ".." row. The
     * top of a server's tree is not the top any more when there are other
     * servers to go back out to. */
    const bool at_root = s_at_server_list ||
                         (s_server_count <= 1U && dlna_browse_stack_at_root(&s_stack));
    unlock();
    return at_root;
}

esp_err_t dlna_source_enter(size_t index)
{
    lock();
    if (!s_open || index >= s_entry_count) {
        unlock();
        return ESP_ERR_INVALID_ARG;
    }
    if (!dlna_browse_stack_enter(&s_stack, &s_entries[index])) {
        unlock();
        return ESP_ERR_INVALID_ARG;
    }
    unlock();

    const esp_err_t err = read_current_container();
    if (err != ESP_OK) {
        /* Back where it was, so a container that would not read leaves the
         * browser showing the one it came from rather than an empty screen
         * with no way out. Only the trail has to be put back: a failed read
         * publishes nothing, so the rows on screen are still the parent's. */
        lock();
        (void)dlna_browse_stack_leave(&s_stack);
        unlock();
    }
    return err;
}

esp_err_t dlna_source_leave(void)
{
    lock();
    if (s_at_server_list) {
        /* Nothing above the servers. The caller reads this as "leave the
         * source", which is the same thing the root of a single server does. */
        unlock();
        return ESP_ERR_NOT_FOUND;
    }
    if (!dlna_browse_stack_leave(&s_stack)) {
        /* At this server's root. With others to choose from, up is the list of
         * them rather than out of the source altogether. */
        if (s_server_count <= 1U) {
            unlock();
            return ESP_ERR_NOT_FOUND;
        }
        s_at_server_list = true;
        fill_server_list();
        unlock();
        return ESP_OK;
    }
    unlock();
    return read_current_container();
}

esp_err_t dlna_source_refresh(void)
{
    return dlna_source_is_open() ? read_current_container() : ESP_ERR_INVALID_STATE;
}

dlna_activate_t dlna_source_activate(size_t index)
{
    dlna_entry_t entry;
    if (!dlna_source_entry_at(index, &entry)) return DLNA_ACTIVATE_REFUSED;

    if (entry.kind == DLNA_ENTRY_CONTAINER) {
        /* One level up from every tree, the rows are servers rather than
         * containers, and opening one means choosing it. This is the only
         * place that has to know the difference - everything that draws the
         * browser sees rows either way. */
        lock();
        const bool choosing_server = s_at_server_list;
        unlock();
        if (choosing_server) {
            return dlna_source_select_server(index) == ESP_OK
                       ? DLNA_ACTIVATE_BROWSED
                       : DLNA_ACTIVATE_BROWSE_FAILED;
        }
        return dlna_source_enter(index) == ESP_OK ? DLNA_ACTIVATE_BROWSED
                                                  : DLNA_ACTIVATE_BROWSE_FAILED;
    }
    return dlna_source_play(index) ? DLNA_ACTIVATE_PLAYING : DLNA_ACTIVATE_REFUSED;
}

bool dlna_source_play(size_t index)
{
    lock();
    if (!s_open || index >= s_entry_count || !s_entries[index].playable) {
        unlock();
        return false;
    }
    /* Backed up one, so that the chain's first call to dlna_next_url() lands
     * on exactly this row. The alternative - starting the chain and then
     * seeking to a row - would play the first track of the album for a moment
     * every time. */
    s_playing = index == 0U ? DLNA_SOURCE_ENTRY_MAX : index - 1U;
    while (s_playing != DLNA_SOURCE_ENTRY_MAX && s_playing > 0U &&
           !s_entries[s_playing].playable) {
        --s_playing;
    }
    if (s_playing != DLNA_SOURCE_ENTRY_MAX && !s_entries[s_playing].playable) {
        s_playing = DLNA_SOURCE_ENTRY_MAX;
    }
    char heading[DLNA_TITLE_MAX];
    snprintf(heading, sizeof(heading), "%s", dlna_browse_stack_title(&s_stack));
    unlock();

    /* The heading is what the player block shows as the "station": for a media
     * server that is the album or folder the tracks came from, and the two
     * functions beside it are what feeds and resumes the chain. */
    if (!internet_radio_start_track_chain(heading, dlna_next_url, dlna_current_url)) {
        lock();
        s_playing = DLNA_SOURCE_ENTRY_MAX;
        unlock();
        return false;
    }
    return true;
}

size_t dlna_source_playing_index(void)
{
    lock();
    const size_t playing = s_playing < s_entry_count ? s_playing : DLNA_SOURCE_ENTRY_MAX;
    unlock();
    return playing;
}

bool dlna_source_playing_point(char *server, size_t server_size, char *container,
                               size_t container_size, char *title, size_t title_size,
                               char *track, size_t track_size)
{
    lock();
    const bool playing = s_open && !s_at_server_list && s_playing < s_entry_count;
    bool ok = playing;
    if (playing) {
        /* The server's uuid rather than its row: the row is whichever order
         * the servers answered the search in this time. */
        if (server != NULL) {
            ok = dlna_ssdp_udn(s_servers[s_selected].usn, server, server_size);
        }
        if (container != NULL) {
            snprintf(container, container_size, "%s", dlna_browse_stack_id(&s_stack));
        }
        if (title != NULL) {
            snprintf(title, title_size, "%s", dlna_browse_stack_title(&s_stack));
        }
        if (track != NULL) {
            snprintf(track, track_size, "%s", s_entries[s_playing].id);
        }
    }
    unlock();
    return ok;
}

void dlna_source_set_resume(const char *server, const char *container, const char *title,
                            const char *track)
{
    lock();
    s_resume_wanted = server != NULL && server[0] != '\0' && container != NULL &&
                      container[0] != '\0';
    snprintf(s_resume_server, sizeof(s_resume_server), "%s", s_resume_wanted ? server : "");
    snprintf(s_resume_container, sizeof(s_resume_container), "%s",
             s_resume_wanted ? container : "");
    snprintf(s_resume_title, sizeof(s_resume_title), "%s",
             s_resume_wanted && title != NULL ? title : "");
    snprintf(s_resume_track, sizeof(s_resume_track), "%s",
             s_resume_wanted && track != NULL ? track : "");
    unlock();
}

/* Which of the servers that answered is the one the resume point names, or
 * DLNA_DISCOVERY_SERVER_MAX when none of them is. Called with the lock held. */
static size_t resume_server_row(void)
{
    for (size_t index = 0U; index < s_server_count; ++index) {
        char udn[DLNA_SSDP_UDN_MAX];
        if (dlna_ssdp_udn(s_servers[index].usn, udn, sizeof(udn)) &&
            strcmp(udn, s_resume_server) == 0) {
            return index;
        }
    }
    return DLNA_DISCOVERY_SERVER_MAX;
}

/* Puts the browser where the resume point says, so that the play that follows
 * lands on the track that was interrupted rather than at the top of the tree.
 *
 * Called after the search, without the lock. Every step can fail on a server
 * whose library has been re-scanned since - the ids are opaque and the server
 * is free to renumber them - so each failure falls back one step instead of
 * failing the open: no such container leaves the browser at the server's root,
 * no such track leaves it in the container with nothing selected, and
 * dlna_source_start_saved() then plays the first playable row it finds. */
static bool apply_resume(void)
{
    lock();
    const size_t row = resume_server_row();
    const bool found = row < s_server_count;
    if (found) {
        s_selected = row;
        s_at_server_list = false;
        dlna_browse_stack_reset(&s_stack, s_servers[row].device.friendly_name);
    }
    const bool at_root = strcmp(s_resume_container, "0") == 0;
    dlna_entry_t level;
    memset(&level, 0, sizeof(level));
    level.kind = DLNA_ENTRY_CONTAINER;
    snprintf(level.id, sizeof(level.id), "%s", s_resume_container);
    snprintf(level.title, sizeof(level.title), "%s",
             s_resume_title[0] != '\0' ? s_resume_title : s_resume_container);
    /* Pushed onto the server's root rather than made the root itself, so that
     * going up from a resumed container lands where it would have if the user
     * had walked in. The levels in between are not known and not worth a
     * request each - the trail above this one is the root. */
    const bool descended = found && !at_root && dlna_browse_stack_enter(&s_stack, &level);
    char track[DLNA_OBJECT_ID_MAX];
    snprintf(track, sizeof(track), "%s", s_resume_track);
    unlock();

    if (!found) {
        /* Not an error: the server may be off, or this may be another network
         * altogether. The open carries on as if nothing had been remembered. */
        ESP_LOGW(TAG, "the remembered media server did not answer this search");
        return false;
    }
    if (read_current_container() != ESP_OK) {
        if (!descended) return true;
        ESP_LOGW(TAG, "the remembered container no longer opens; starting at the root");
        lock();
        (void)dlna_browse_stack_leave(&s_stack);
        unlock();
        (void)read_current_container();
        return true;
    }
    if (track[0] == '\0') return true;

    lock();
    for (size_t index = 0U; index < s_entry_count; ++index) {
        if (s_entries[index].playable && strcmp(s_entries[index].id, track) == 0) {
            /* Not started here - only pointed at. The play arrives as its own
             * command, which is what keeps merely selecting the source from
             * making a noise. */
            s_playing = index;
            break;
        }
    }
    unlock();
    return true;
}

bool dlna_source_start_saved(void)
{
    lock();
    size_t index = s_playing;
    if (index >= s_entry_count || !s_entries[index].playable) {
        index = 0U;
        while (index < s_entry_count && !s_entries[index].playable) ++index;
    }
    const bool have = index < s_entry_count;
    unlock();
    return have && dlna_source_play(index);
}

bool dlna_source_skip(bool forward)
{
    if (forward) {
        /* The audio path already knows how to end a track early and ask for
         * the next link, which is exactly what this is. */
        return internet_radio_skip_track();
    }

    lock();
    if (s_playing >= s_entry_count) {
        unlock();
        return false;
    }
    size_t index = s_playing;
    while (index > 0U) {
        --index;
        if (s_entries[index].playable) break;
    }
    if (index == s_playing || !s_entries[index].playable) {
        unlock();
        return false;
    }
    unlock();
    return dlna_source_play(index);
}
