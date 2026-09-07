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
static size_t s_entry_count;
static bool s_open;
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
static void keep_music_sections(void)
{
    size_t kept = 0U;
    for (size_t index = 0U; index < s_entry_count; ++index) {
        if (s_entries[index].kind == DLNA_ENTRY_CONTAINER &&
            !dlna_root_filter_is_music(s_entries[index].title)) {
            continue;
        }
        if (kept != index) s_entries[kept] = s_entries[index];
        ++kept;
    }
    if (kept == 0U) {
        ESP_LOGI(TAG, "no section of the root looks like music; showing all %u",
                 (unsigned)s_entry_count);
        return;
    }
    if (kept < s_entry_count) {
        ESP_LOGI(TAG, "root: %u of %u sections kept", (unsigned)kept,
                 (unsigned)s_entry_count);
        s_entry_count = kept;
    }
}

/* Reads the container the stack is pointing at, a page at a time, until the
 * listing is full or the server has no more to give.
 *
 * Called with the lock held. A failed read empties the listing rather than
 * leaving the previous container's rows under the new one's heading, which
 * would be a browser offering to play files from somewhere else. */
static esp_err_t read_current_container(void)
{
    s_entry_count = 0U;
    s_playing = DLNA_SOURCE_ENTRY_MAX;

    const char *control_url = s_servers[s_selected].device.control_url;
    const char *object_id = dlna_browse_stack_id(&s_stack);

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
                                                 page_size, s_entries + s_entry_count,
                                                 DLNA_SOURCE_ENTRY_MAX - s_entry_count,
                                                 &page);
        if (err == ESP_ERR_INVALID_SIZE) {
            /* Ask again for half as much, from the same place. What has been
             * read so far is kept - it is complete and correct, the answer was
             * only cut after it. One entry that will not fit on its own is a
             * container this device genuinely cannot read. */
            if (page_size <= 1U) {
                ESP_LOGW(TAG, "'%s' says more about one entry than fits in a browse",
                         dlna_browse_stack_title(&s_stack));
                s_entry_count = 0U;
                return err;
            }
            page_size /= 2U;
            continue;
        }
        if (err != ESP_OK) {
            s_entry_count = 0U;
            return err;
        }

        s_entry_count += page.count;
        /* Advanced by what was *asked for* rather than by what came back: rows
         * the parser could not use still occupy the server's numbering, and
         * counting only the kept ones would ask for the same page for ever. */
        starting_index += page_size;

        if (starting_index >= page.total_matches) break;
        if (s_entry_count >= DLNA_SOURCE_ENTRY_MAX) {
            ESP_LOGW(TAG, "'%s' holds %u entries; showing the first %u",
                     dlna_browse_stack_title(&s_stack), (unsigned)page.total_matches,
                     (unsigned)s_entry_count);
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
    if (dlna_browse_stack_at_root(&s_stack)) keep_music_sections();
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
    if (s_entries == NULL) {
        s_entries = heap_caps_malloc(DLNA_SOURCE_ENTRY_MAX * sizeof(*s_entries),
                                     MALLOC_CAP_SPIRAM);
        if (s_entries == NULL) {
            s_entries = heap_caps_malloc(DLNA_SOURCE_ENTRY_MAX * sizeof(*s_entries),
                                         MALLOC_CAP_INTERNAL);
        }
        if (s_entries == NULL) {
            s_searching = false;
            unlock();
            ESP_LOGE(TAG, "no memory for a listing of %u entries",
                     (unsigned)DLNA_SOURCE_ENTRY_MAX);
            dlna_client_close();
            return ESP_ERR_NO_MEM;
        }
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
    if (!s_open) {
        s_searching = false;
        unlock();
        return ESP_ERR_NOT_FOUND;
    }
    dlna_browse_stack_reset(&s_stack, s_servers[0].device.friendly_name);
    err = read_current_container();
    s_searching = false;
    unlock();

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
    free(s_entries);
    s_entries = NULL;
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
    /* At the root, because a position in one server's tree names nothing in
     * another's - the ids are the server's own. */
    dlna_browse_stack_reset(&s_stack, s_servers[index].device.friendly_name);
    const esp_err_t err = read_current_container();
    unlock();
    return err;
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
     * caller reads it to draw it immediately. */
    return dlna_browse_stack_title(&s_stack);
}

bool dlna_source_at_root(void)
{
    lock();
    const bool at_root = dlna_browse_stack_at_root(&s_stack);
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
    esp_err_t err = read_current_container();
    if (err != ESP_OK) {
        /* Back where it was, so a container that would not read leaves the
         * browser showing the one it came from rather than an empty screen
         * with no way out. */
        (void)dlna_browse_stack_leave(&s_stack);
        (void)read_current_container();
    }
    unlock();
    return err;
}

esp_err_t dlna_source_leave(void)
{
    lock();
    if (!dlna_browse_stack_leave(&s_stack)) {
        unlock();
        return ESP_ERR_NOT_FOUND;
    }
    const esp_err_t err = read_current_container();
    unlock();
    return err;
}

esp_err_t dlna_source_refresh(void)
{
    lock();
    const esp_err_t err = s_open ? read_current_container() : ESP_ERR_INVALID_STATE;
    unlock();
    return err;
}

dlna_activate_t dlna_source_activate(size_t index)
{
    dlna_entry_t entry;
    if (!dlna_source_entry_at(index, &entry)) return DLNA_ACTIVATE_REFUSED;

    if (entry.kind == DLNA_ENTRY_CONTAINER) {
        return dlna_source_enter(index) == ESP_OK ? DLNA_ACTIVATE_BROWSED
                                                  : DLNA_ACTIVATE_REFUSED;
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
