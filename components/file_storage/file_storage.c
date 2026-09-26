#include "file_storage.h"

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "cue_sheet.h"
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

/* A sheet is text and small - the one it was written for is 1 KB - so the
 * whole file is read at once; anything past this is not a cue sheet. */
#define FILE_STORAGE_CUE_MAX_BYTES (64U * 1024U)

static file_browser_entry_t *s_entries;
/* The titles of the .cue tracks in the listing, beside it rather than in it:
 * file_browser_entry_t is copied onto the stacks of the player task, the UI
 * and the web server, and two more strings would cost each of them 256 bytes
 * for something only a cue track has. Found by sheet and track number, not
 * by row, so the rows can move under them. PSRAM, like the listing. */
typedef struct {
    uint8_t sheet;
    uint8_t number;
    char title[CUE_SHEET_TEXT_MAX];
    char performer[CUE_SHEET_TEXT_MAX];
} file_storage_cue_text_t;
static file_storage_cue_text_t *s_cue_text;
static size_t s_cue_text_count;
/* The sheets the listing's cue tracks came from, by entry.cue_sheet - 1: the
 * path a resume point names the track by, and the album. A folder with more
 * sheets than this shows the rest as CUE rows. */
#define FILE_STORAGE_CUE_SHEETS_MAX 8U
typedef struct {
    char path[FILE_BROWSER_PATH_MAX_LEN];
    char album[CUE_SHEET_TEXT_MAX];
} file_storage_cue_sheet_t;
static file_storage_cue_sheet_t *s_cue_sheets;
static size_t s_cue_sheet_count;
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
    const size_t text_bytes = FILE_STORAGE_MAX_ENTRIES * sizeof(file_storage_cue_text_t);
    s_cue_text = heap_caps_calloc(1U, text_bytes, MALLOC_CAP_SPIRAM);
    if (s_cue_text == NULL) s_cue_text = heap_caps_calloc(1U, text_bytes, MALLOC_CAP_INTERNAL);
    // Not fatal: a .cue then lists without titles, the tracks still play.
    if (s_cue_text == NULL) ESP_LOGW(TAG, "no memory for .cue titles");
    // PSRAM only, 9 KB: without it a folder shows its .cue as a CUE row.
    s_cue_sheets = heap_caps_calloc(FILE_STORAGE_CUE_SHEETS_MAX, sizeof(*s_cue_sheets),
                                    MALLOC_CAP_SPIRAM);
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

/* Reads and parses the sheet at `path` into a PSRAM block the caller frees;
 * NULL when it is not a sheet with a playable track. The sheet alone is
 * ~30 KB and every caller runs on the player task. */
static cue_sheet_t *cue_sheet_load(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        ESP_LOGE(TAG, "cannot open %s", path);
        return NULL;
    }
    long size = -1;
    if (fseek(file, 0, SEEK_END) == 0) size = ftell(file);
    if (size <= 0 || (unsigned long)size > FILE_STORAGE_CUE_MAX_BYTES ||
        fseek(file, 0, SEEK_SET) != 0) {
        ESP_LOGW(TAG, "%s: %ld bytes is not a cue sheet", path, size);
        fclose(file);
        return NULL;
    }
    uint8_t *text = heap_caps_malloc((size_t)size, MALLOC_CAP_SPIRAM);
    cue_sheet_t *sheet = heap_caps_malloc(sizeof(*sheet), MALLOC_CAP_SPIRAM);
    if (text == NULL || sheet == NULL) {
        free(text);
        free(sheet);
        fclose(file);
        return NULL;
    }
    const size_t read = fread(text, 1U, (size_t)size, file);
    fclose(file);
    const bool parsed = read == (size_t)size && cue_sheet_parse(text, read, sheet);
    free(text);
    if (!parsed) {
        ESP_LOGW(TAG, "%s: no playable track in the sheet", path);
        free(sheet);
        return NULL;
    }
    return sheet;
}

// Forgets the sheets of the listing before it is read again. Under the lock.
static void cue_sheets_clear(void)
{
    s_cue_sheet_count = 0U;
    s_cue_text_count = 0U;
}

/* Keeps a sheet's path, album and titles as sheet `id`. Under the lock. A
 * title that does not fit only costs its row the name. */
static void cue_sheet_remember(uint8_t id, const char *path, const cue_sheet_t *sheet)
{
    if (s_cue_sheets != NULL && id >= 1U && id <= FILE_STORAGE_CUE_SHEETS_MAX) {
        file_storage_cue_sheet_t *kept = &s_cue_sheets[id - 1U];
        snprintf(kept->path, sizeof(kept->path), "%s", path);
        snprintf(kept->album, sizeof(kept->album), "%s", sheet->title);
    }
    if (s_cue_text == NULL) return;
    for (size_t i = 0U; i < sheet->track_count && s_cue_text_count < FILE_STORAGE_MAX_ENTRIES;
         ++i) {
        const cue_sheet_track_t *track = &sheet->tracks[i];
        file_storage_cue_text_t *row = &s_cue_text[s_cue_text_count++];
        row->sheet = id;
        row->number = track->number;
        snprintf(row->title, sizeof(row->title), "%s", track->title);
        // A track without a performer of its own is the album's.
        snprintf(row->performer, sizeof(row->performer), "%s",
                 track->performer[0] != '\0' ? track->performer : sheet->performer);
    }
}

/* Puts the tracks of every .cue in the directory in place of the sheet and
 * the files it cuts up - see file_browser_dir_expand_cue(). The sheets are
 * read without the lock held, since a read is a USB transfer; the listing is
 * only this task's to change meanwhile. */
static void file_storage_expand_cue_sheets(const char *path)
{
    if (s_cue_sheets == NULL) return;
    char *sheet_path = heap_caps_malloc(FILE_BROWSER_PATH_MAX_LEN, MALLOC_CAP_SPIRAM);
    if (sheet_path == NULL) return;
    size_t row = 0U;
    while (s_cue_sheet_count < FILE_STORAGE_CUE_SHEETS_MAX) {
        file_browser_entry_t entry;
        bool is_sheet = false;
        if (!listing_lock()) break;
        const file_browser_entry_t *found = file_browser_dir_entry(&s_listing, row);
        if (found != NULL) {
            entry = *found;
            is_sheet = entry.kind == FILE_BROWSER_ENTRY_PLAYLIST &&
                       playlist_file_kind_from_name(entry.name) == PLAYLIST_FILE_CUE;
        }
        listing_unlock();
        if (found == NULL) break;
        cue_sheet_t *sheet = NULL;
        if (is_sheet &&
            file_browser_path_child(path, entry.name, sheet_path, FILE_BROWSER_PATH_MAX_LEN)) {
            sheet = cue_sheet_load(sheet_path);
        }
        size_t added = 0U;
        if (sheet != NULL && listing_lock()) {
            const uint8_t id = (uint8_t)(s_cue_sheet_count + 1U);
            added = file_browser_dir_expand_cue(&s_listing, row, sheet, id);
            if (added > 0U) {
                ++s_cue_sheet_count;
                cue_sheet_remember(id, sheet_path, sheet);
            }
            listing_unlock();
            ESP_LOGI(TAG, "%s: %u tracks in the folder", entry.name, (unsigned)added);
        }
        free(sheet);
        // The sheet's row is gone when it was expanded; what follows moved up.
        if (added == 0U) ++row;
    }
    free(sheet_path);
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
    cue_sheets_clear();
    const struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        file_browser_dir_add(&s_listing, entry->d_name, entry_kind(path, entry));
    }
    file_browser_dir_sort(&s_listing);
    listing_unlock();
    closedir(dir);
    file_storage_expand_cue_sheets(path);
    if (!listing_lock()) return ESP_ERR_TIMEOUT;
    const size_t count = s_listing.count;
    const size_t dropped_full = s_listing.dropped_full;
    const size_t dropped_long = s_listing.dropped_long_name;
    listing_unlock();
    if (dropped_full > 0U || dropped_long > 0U) {
        ESP_LOGW(TAG, "%s: %u entries dropped (listing full=%u, name too long=%u)", path,
                 (unsigned)(dropped_full + dropped_long), (unsigned)dropped_full,
                 (unsigned)dropped_long);
    }
    ESP_LOGI(TAG, "%s: %u entries", path, (unsigned)count);
    return ESP_OK;
}

/* A .cue sheet as a listing: one row per track, each pointing at the file the
 * track lies in, with where it starts and ends there. Rows keep the sheet's
 * order - it is the playing order - and several rows name the same file. */
static esp_err_t file_storage_read_cue(const char *path)
{
    cue_sheet_t *sheet = cue_sheet_load(path);
    if (sheet == NULL) return ESP_FAIL;
    if (!listing_lock()) {
        free(sheet);
        return ESP_ERR_TIMEOUT;
    }
    file_browser_dir_init_playlist(&s_listing, s_entries, FILE_STORAGE_MAX_ENTRIES, path);
    cue_sheets_clear();
    for (size_t i = 0U; i < sheet->track_count; ++i) {
        const cue_sheet_track_t *track = &sheet->tracks[i];
        (void)file_browser_dir_add_cue_track(&s_listing, sheet->files[track->file], track->number,
                                             track->start_frames,
                                             cue_sheet_track_end_frames(sheet, i));
    }
    s_cue_sheet_count = 1U;
    cue_sheet_remember(1U, path, sheet);
    const size_t count = s_listing.count;
    const size_t dropped = s_listing.dropped_full + s_listing.dropped_long_name +
                           s_listing.dropped_unplayable + sheet->dropped;
    listing_unlock();
    free(sheet);
    if (dropped > 0U) ESP_LOGW(TAG, "%s: %u tracks dropped", path, (unsigned)dropped);
    ESP_LOGI(TAG, "%s: %u tracks", path, (unsigned)count);
    return ESP_OK;
}

esp_err_t file_storage_read_playlist(const char *path)
{
    if (path == NULL || s_entries == NULL) return ESP_ERR_INVALID_ARG;
    const playlist_file_kind_t kind = playlist_file_kind_from_name(path);
    if (kind == PLAYLIST_FILE_NONE) return ESP_ERR_INVALID_ARG;
    // Asked before the open, for the reason read_directory gives.
    if (!file_storage_path_mounted(path)) return ESP_ERR_INVALID_STATE;
    if (kind == PLAYLIST_FILE_CUE) return file_storage_read_cue(path);
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
    cue_sheets_clear();
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

// Under the lock.
static const file_storage_cue_text_t *cue_text_for(const file_browser_entry_t *entry)
{
    if (s_cue_text == NULL || entry == NULL || entry->cue_track == 0U) return NULL;
    for (size_t i = 0U; i < s_cue_text_count; ++i) {
        if (s_cue_text[i].sheet == entry->cue_sheet && s_cue_text[i].number == entry->cue_track) {
            return &s_cue_text[i];
        }
    }
    return NULL;
}

// Under the lock.
static const file_storage_cue_sheet_t *cue_sheet_of(uint8_t id)
{
    if (s_cue_sheets == NULL || id == 0U || id > s_cue_sheet_count) return NULL;
    return &s_cue_sheets[id - 1U];
}

bool file_storage_entry_title(size_t index, char *title, size_t title_size, char *performer,
                              size_t performer_size)
{
    if (title != NULL && title_size > 0U) title[0] = '\0';
    if (performer != NULL && performer_size > 0U) performer[0] = '\0';
    if (!listing_lock()) return false;
    const file_storage_cue_text_t *text = cue_text_for(file_browser_dir_entry(&s_listing, index));
    if (text != NULL) {
        if (title != NULL && title_size > 0U) snprintf(title, title_size, "%s", text->title);
        if (performer != NULL && performer_size > 0U) {
            snprintf(performer, performer_size, "%s", text->performer);
        }
    }
    listing_unlock();
    return text != NULL;
}

void file_storage_entry_label(size_t index, const file_browser_entry_t *entry, char *out,
                              size_t out_size)
{
    if (out == NULL || out_size == 0U) return;
    out[0] = '\0';
    if (entry == NULL) return;
    if (entry->cue_track != 0U &&
        file_storage_entry_title(index, out, out_size, NULL, 0U) && out[0] != '\0') {
        return;
    }
    if (entry->cue_track != 0U) {
        snprintf(out, out_size, "%02u - %s", (unsigned)entry->cue_track,
                 file_browser_display_name(entry->name));
        return;
    }
    snprintf(out, out_size, "%s", file_browser_display_name(entry->name));
}

bool file_storage_cue_sheet(uint8_t sheet, char *path, size_t path_size, char *album,
                            size_t album_size)
{
    if (path != NULL && path_size > 0U) path[0] = '\0';
    if (album != NULL && album_size > 0U) album[0] = '\0';
    if (!listing_lock()) return false;
    const file_storage_cue_sheet_t *kept = cue_sheet_of(sheet);
    if (kept != NULL) {
        if (path != NULL && path_size > 0U) snprintf(path, path_size, "%s", kept->path);
        if (album != NULL && album_size > 0U) snprintf(album, album_size, "%s", kept->album);
    }
    listing_unlock();
    return kept != NULL;
}

size_t file_storage_find_cue_track(const char *sheet_path, uint8_t number)
{
    if (sheet_path == NULL || number == 0U || !listing_lock()) return SIZE_MAX;
    size_t found = SIZE_MAX;
    for (size_t i = 0U; i < s_listing.count; ++i) {
        const file_browser_entry_t *entry = &s_listing.entries[i];
        const file_storage_cue_sheet_t *kept = cue_sheet_of(entry->cue_sheet);
        if (entry->cue_track == number && kept != NULL && strcmp(kept->path, sheet_path) == 0) {
            found = i;
            break;
        }
    }
    listing_unlock();
    return found;
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
