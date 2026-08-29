#include "file_storage.h"

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "files";

// One directory is on screen at a time, so one shared listing is enough. At
// 264 bytes per entry this is ~68 KB, which is why it lives in PSRAM.
#define FILE_STORAGE_MAX_ENTRIES 256

/* One playlist line. Long enough for the longest path the browser handles
 * plus the "File99=" a .pls puts in front of it; a line that still does not
 * fit is dropped and counted, the way an over-long file name is. */
#define FILE_STORAGE_LINE_MAX (FILE_BROWSER_PATH_MAX_LEN + 16)

// Two: the USB drive and the SD card, which is also all FATFS is built for
// (CONFIG_FATFS_VOLUME_COUNT).
#define FILE_STORAGE_MAX_VOLUMES 2

typedef struct {
    const char *root;
    file_storage_mounted_fn mounted;
} file_storage_volume_t;

static file_browser_entry_t *s_entries;
static file_browser_dir_t s_listing;
static SemaphoreHandle_t s_listing_lock;
static file_storage_volume_t s_volumes[FILE_STORAGE_MAX_VOLUMES];
static size_t s_volume_count;

static bool listing_lock(void)
{
    return s_listing_lock != NULL &&
           xSemaphoreTake(s_listing_lock, pdMS_TO_TICKS(1000)) == pdTRUE;
}

static void listing_unlock(void)
{
    if (s_listing_lock != NULL) xSemaphoreGive(s_listing_lock);
}

esp_err_t file_storage_init(void)
{
    if (s_entries != NULL) {
        return ESP_OK;
    }
    s_listing_lock = xSemaphoreCreateMutex();
    if (s_listing_lock == NULL) return ESP_ERR_NO_MEM;
    // Internal SRAM headroom is tight and this block is large, so prefer PSRAM
    // and keep the internal fallback for boards without it.
    const size_t listing_bytes = FILE_STORAGE_MAX_ENTRIES * sizeof(file_browser_entry_t);
    s_entries = heap_caps_malloc(listing_bytes, MALLOC_CAP_SPIRAM);
    if (s_entries == NULL) s_entries = heap_caps_malloc(listing_bytes, MALLOC_CAP_INTERNAL);
    if (s_entries == NULL) {
        vSemaphoreDelete(s_listing_lock);
        s_listing_lock = NULL;
        return ESP_ERR_NO_MEM;
    }
    file_browser_dir_init(&s_listing, s_entries, FILE_STORAGE_MAX_ENTRIES, "");
    return ESP_OK;
}

esp_err_t file_storage_register_volume(const char *root, file_storage_mounted_fn mounted)
{
    if (root == NULL || mounted == NULL) return ESP_ERR_INVALID_ARG;
    if (s_volume_count >= FILE_STORAGE_MAX_VOLUMES) return ESP_ERR_NO_MEM;
    // The root string is kept by pointer, not copied: every caller passes a
    // string literal from its own header.
    s_volumes[s_volume_count].root = root;
    s_volumes[s_volume_count].mounted = mounted;
    ++s_volume_count;
    return ESP_OK;
}

bool file_storage_path_mounted(const char *path)
{
    if (path == NULL) return false;
    for (size_t index = 0U; index < s_volume_count; ++index) {
        if (file_browser_path_on_volume(path, s_volumes[index].root)) {
            return s_volumes[index].mounted();
        }
    }
    return false;
}

static file_browser_entry_kind_t entry_kind(const char *path, const struct dirent *entry)
{
    // FATFS fills d_type, but fall back to stat() rather than trusting it: a
    // directory misread as a file becomes an unplayable "track", and one
    // misread the other way hides everything below it.
    if (entry->d_type == DT_DIR) return FILE_BROWSER_ENTRY_DIRECTORY;
    if (entry->d_type == DT_REG) return FILE_BROWSER_ENTRY_FILE;
    char child[FILE_BROWSER_PATH_MAX_LEN];
    struct stat info;
    if (file_browser_path_child(path, entry->d_name, child, sizeof(child)) &&
        stat(child, &info) == 0 && S_ISDIR(info.st_mode)) {
        return FILE_BROWSER_ENTRY_DIRECTORY;
    }
    return FILE_BROWSER_ENTRY_FILE;
}

esp_err_t file_storage_read_directory(const char *path)
{
    if (path == NULL || s_entries == NULL) return ESP_ERR_INVALID_ARG;
    /* Asked before opendir() so that "the card is not in the slot" cannot come
     * back as "that directory does not exist". The caller shows the two
     * differently, and a volume that has just been mounted answers true here
     * before anything has been read from it. */
    if (!file_storage_path_mounted(path)) return ESP_ERR_INVALID_STATE;
    DIR *dir = opendir(path);
    if (dir == NULL) {
        ESP_LOGE(TAG, "cannot open %s", path);
        return ESP_FAIL;
    }
    if (!listing_lock()) {
        closedir(dir);
        return ESP_ERR_TIMEOUT;
    }
    file_browser_dir_init(&s_listing, s_entries, FILE_STORAGE_MAX_ENTRIES, path);
    const struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        file_browser_dir_add(&s_listing, entry->d_name, entry_kind(path, entry));
    }
    file_browser_dir_sort(&s_listing);
    const size_t count = s_listing.count;
    const size_t dropped_full = s_listing.dropped_full;
    const size_t dropped_long = s_listing.dropped_long_name;
    listing_unlock();
    closedir(dir);
    if (dropped_full > 0U || dropped_long > 0U) {
        ESP_LOGW(TAG, "%s: %u entries dropped (listing full=%u, name too long=%u)", path,
                 (unsigned)(dropped_full + dropped_long), (unsigned)dropped_full,
                 (unsigned)dropped_long);
    }
    ESP_LOGI(TAG, "%s: %u entries", path, (unsigned)count);
    return ESP_OK;
}

esp_err_t file_storage_read_playlist(const char *path)
{
    if (path == NULL || s_entries == NULL) return ESP_ERR_INVALID_ARG;
    const playlist_file_kind_t kind = playlist_file_kind_from_name(path);
    if (kind == PLAYLIST_FILE_NONE) return ESP_ERR_INVALID_ARG;
    // Asked before the open, for the reason read_directory gives.
    if (!file_storage_path_mounted(path)) return ESP_ERR_INVALID_STATE;
    /* The line buffer is a kilobyte, so it is allocated rather than put on the
     * stack: every caller of this is the player_control task, whose stack is
     * the tightest on the device. PSRAM first like every other large block,
     * and freed before returning - a playlist is opened by a keypress, not in
     * a loop. */
    char *line = heap_caps_malloc(FILE_STORAGE_LINE_MAX, MALLOC_CAP_SPIRAM);
    if (line == NULL) line = heap_caps_malloc(FILE_STORAGE_LINE_MAX, MALLOC_CAP_INTERNAL);
    if (line == NULL) return ESP_ERR_NO_MEM;
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        ESP_LOGE(TAG, "cannot open %s", path);
        free(line);
        return ESP_FAIL;
    }
    if (!listing_lock()) {
        fclose(file);
        free(line);
        return ESP_ERR_TIMEOUT;
    }
    file_browser_dir_init_playlist(&s_listing, s_entries, FILE_STORAGE_MAX_ENTRIES, path);
    while (fgets(line, FILE_STORAGE_LINE_MAX, file) != NULL) {
        const bool whole_line = strchr(line, '\n') != NULL || feof(file);
        const char *reference = NULL;
        const playlist_file_line_t parsed = playlist_file_read_line(kind, line, &reference);
        if (!whole_line) {
            /* Only the head of the line is here. Drop the rest of it, and count
             * the entry only if that head was going somewhere: an #EXTINF
             * comment longer than the buffer costs the listing nothing, and
             * reporting it as a lost track would be a lie. */
            int discarded;
            while ((discarded = fgetc(file)) != EOF && discarded != '\n') {
            }
            if (parsed != PLAYLIST_FILE_LINE_IGNORED) ++s_listing.dropped_long_name;
            continue;
        }
        if (parsed == PLAYLIST_FILE_LINE_UNPLAYABLE) {
            ++s_listing.dropped_unplayable;
            continue;
        }
        if (parsed != PLAYLIST_FILE_LINE_TRACK) continue;
        /* dir_add refuses a file in a format nothing here decodes, and says so
         * by returning false without counting it - which is right for a
         * directory, where a .txt beside the music was never a track. In a
         * playlist it is one the user asked for and will not get. */
        if (!file_browser_dir_add(&s_listing, reference, FILE_BROWSER_ENTRY_FILE) &&
            file_browser_format_from_name(reference) == FILE_BROWSER_FORMAT_NONE) {
            ++s_listing.dropped_unplayable;
        }
    }
    // Deliberately not sorted: the order the file gives is the playing order.
    const size_t count = s_listing.count;
    const size_t dropped_full = s_listing.dropped_full;
    const size_t dropped_long = s_listing.dropped_long_name;
    const size_t dropped_unplayable = s_listing.dropped_unplayable;
    listing_unlock();
    fclose(file);
    free(line);
    if (dropped_full > 0U || dropped_long > 0U || dropped_unplayable > 0U) {
        ESP_LOGW(TAG, "%s: %u entries dropped (listing full=%u, line too long=%u, "
                      "cannot be played=%u)",
                 path, (unsigned)(dropped_full + dropped_long + dropped_unplayable),
                 (unsigned)dropped_full, (unsigned)dropped_long,
                 (unsigned)dropped_unplayable);
    }
    ESP_LOGI(TAG, "%s: %u tracks", path, (unsigned)count);
    return ESP_OK;
}

esp_err_t file_storage_open(const char *path)
{
    return playlist_file_kind_from_name(path) != PLAYLIST_FILE_NONE
               ? file_storage_read_playlist(path)
               : file_storage_read_directory(path);
}

bool file_storage_listing_is_on(const char *root)
{
    if (s_entries == NULL || root == NULL || !listing_lock()) return false;
    const bool same = file_browser_path_on_volume(s_listing.path, root);
    listing_unlock();
    return same;
}

void file_storage_open_empty(const char *root)
{
    if (s_entries == NULL || root == NULL || !listing_lock()) return;
    file_browser_dir_init(&s_listing, s_entries, FILE_STORAGE_MAX_ENTRIES, root);
    listing_unlock();
}

size_t file_storage_entry_count(void)
{
    if (!listing_lock()) return 0U;
    const size_t count = s_listing.count;
    listing_unlock();
    return count;
}

bool file_storage_entry_at(size_t index, file_browser_entry_t *out)
{
    if (out == NULL || !listing_lock()) return false;
    const file_browser_entry_t *entry = file_browser_dir_entry(&s_listing, index);
    if (entry != NULL) *out = *entry;
    listing_unlock();
    return entry != NULL;
}

size_t file_storage_find_entry(const char *name)
{
    // Same "past the end on failure" contract as next_file, and for the same
    // reason: the caller falls back to the browser rather than opening
    // whatever happens to sit at index 0.
    if (!listing_lock()) return SIZE_MAX;
    const size_t index = file_browser_dir_find(&s_listing, name);
    listing_unlock();
    return index;
}

size_t file_storage_next_file(size_t from)
{
    // SIZE_MAX, not 0: the caller advances to whatever comes back and only
    // stops when it is past the end, so returning 0 here would restart the
    // directory from its first track instead of stopping. Failing "past the
    // end" makes a lock timeout indistinguishable from "nothing left", which
    // is the safe direction to be wrong in.
    if (!listing_lock()) return SIZE_MAX;
    const size_t index = file_browser_dir_next_file(&s_listing, from);
    listing_unlock();
    return index;
}

size_t file_storage_previous_file(size_t before)
{
    // Same contract as next_file above, including SIZE_MAX on a lock timeout.
    if (!listing_lock()) return SIZE_MAX;
    const size_t index = file_browser_dir_previous_file(&s_listing, before);
    listing_unlock();
    return index;
}

bool file_storage_entry_path(size_t index, file_browser_entry_t *entry, char *path,
                             size_t capacity)
{
    if (entry == NULL || path == NULL || capacity == 0U || !listing_lock()) return false;
    const file_browser_entry_t *found = file_browser_dir_entry(&s_listing, index);
    bool built = false;
    if (found != NULL) {
        *entry = *found;
        // Pure string work, so the lock is held for no longer than a copy.
        // Which of the two listings is open decides what the name means, and
        // dir_path_for is the one place that knows.
        built = file_browser_dir_path_for(&s_listing, found->name, path, capacity);
    }
    listing_unlock();
    return built;
}

bool file_storage_current_path(char *out, size_t out_size)
{
    if (out == NULL || out_size == 0U || !listing_lock()) return false;
    const size_t length = strlen(s_listing.path);
    const bool fits = length < out_size;
    if (fits) memcpy(out, s_listing.path, length + 1U);
    listing_unlock();
    return fits;
}

/* Copies each entry out before printing it. Holding the lock across the whole
 * loop meant holding it across the UART: a 256-entry directory is some 15 KB of
 * log, over a second at 115200 baud, which is longer than the 1 s other callers
 * wait for the lock. Track advance ran into exactly that. */
void file_storage_log_listing(void)
{
    char path[FILE_BROWSER_PATH_MAX_LEN];
    if (!file_storage_current_path(path, sizeof(path))) return;
    ESP_LOGI(TAG, "files on %s:", path);
    const size_t count = file_storage_entry_count();
    for (size_t i = 0U; i < count; ++i) {
        file_browser_entry_t entry;
        if (!file_storage_entry_at(i, &entry)) break;
        if (entry.kind == FILE_BROWSER_ENTRY_DIRECTORY) {
            ESP_LOGI(TAG, "  [dir] %s", entry.name);
        } else {
            ESP_LOGI(TAG, "  %-4s %s", file_browser_entry_type_label(&entry), entry.name);
        }
    }
}
