#include "file_player.h"

#include <dirent.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "album_art.h"
#include "audio_pcm_convert.h"
#include "audio_tags_reader.h"
#include "board.h"
#include "cover_file.h"
#include "flac_tags.h"
#include "radio_decoder.h"
#include "board_audio_format.h"
#include "radio_prebuffer.h"
#include "file_track_progress.h"
#include "radio_stream_format.h"
#include "file_storage.h"
#include "file_wav.h"

static const char *TAG = "file_player";

// A local file never stalls the way a network stream does, so none of the
// radio's prebuffering applies here. The input buffer only has to hold one
// worst-case compressed frame plus slack; FLAC frames are the large ones.
#define FILE_PLAYER_INPUT_SIZE 32768U
#define FILE_PLAYER_READ_CHUNK 4096U
/* The stdio buffer of the track being played. newlib as ESP-IDF builds it
 * refills a FILE from its buffer's size and nothing larger: the default 128
 * bytes made every sector its own USB command, and the drive gave 420 KB/s
 * whatever fread was asked for - short of what a 96 kHz/24-bit FLAC needs
 * (340 KB/s) with time left to decode it. In 4 KB pieces it gave 1000 KB/s.
 * Unbuffered is worse still: it reads a byte at a time.
 *
 * Not larger, though 16 KB measured a little faster: usb_host_msc grows its
 * transfer buffer to the largest read it has been asked for and keeps it,
 * in internal DMA memory. 16 KB there left the largest internal block at
 * 7 KB, below what TLS needs. */
#define FILE_PLAYER_STDIO_BUFFER 4096U
/* Where the output buffer starts, not where it stays: enough for a 4096-sample
 * stereo frame, which is what most files decode to, and grown to fit when a
 * header asks for more - see grow_pcm_buffer(). */
#define FILE_PLAYER_PCM_SIZE 16384U
#define FILE_PLAYER_STOP_TIMEOUT_MS 5000U
// Pinned to core 1 for the same reason as the radio decoder: core 0 carries
// Wi-Fi, lwIP, the HTTP server and LVGL, and lwIP at priority 18 preempts the
// decoder mid-frame when it shares a core.
#define FILE_PLAYER_TASK_CORE 1
#define FILE_PLAYER_TASK_PRIORITY 6
#define FILE_PLAYER_TASK_STACK 8192

static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
static file_player_status_t s_status;
// Under the same lock as the status, but kept out of it - see file_player.h.
static audio_tags_t s_tags;
/* Only ever increases, and deliberately not part of s_status, which is cleared
 * whole when a track starts: a counter that went back to zero would look
 * unchanged to a watcher that had seen zero before. */
static atomic_uint s_tags_revision = ATOMIC_VAR_INIT(0);
/* Written by the playback task on every pass and read by whoever asks for
 * status. Atomic rather than inside the status critical section: the loop runs
 * thousands of times a second and this is one byte of display. */
static atomic_uint s_input_fill_percent = ATOMIC_VAR_INIT(0);
/* Position and length in seconds, written per block by the playback task. */
static atomic_uint s_elapsed_seconds = ATOMIC_VAR_INIT(0);
static atomic_uint s_total_seconds = ATOMIC_VAR_INIT(0);
/* Where the listener has asked the track to jump to, in seconds, or
 * FILE_PLAYER_SEEK_NONE. Handed to the playback task rather than acted on by
 * the caller: only that task owns the file handle, the decoder and the byte
 * counters a jump has to move together. */
#define FILE_PLAYER_SEEK_NONE UINT32_MAX
static atomic_uint s_seek_request = ATOMIC_VAR_INIT(FILE_PLAYER_SEEK_NONE);
static SemaphoreHandle_t s_control_lock;
static atomic_bool s_stop_requested = ATOMIC_VAR_INIT(false);
static atomic_bool s_paused = ATOMIC_VAR_INIT(false);
/* True from just before the playback task is created until it has finished.
 * Set by the starter rather than by the task itself: the task is pinned to the
 * other core and need not have run by the time file_player_play() returns, so a
 * stop() or a second play() arriving in that window would sail straight through
 * wait_for_track_end() and leave two tasks sharing s_request and the I2S
 * output. */
static atomic_bool s_task_running = ATOMIC_VAR_INIT(false);

typedef struct {
    char path[FILE_BROWSER_PATH_MAX_LEN];
    file_browser_format_t format;
} file_player_request_t;

static file_player_request_t s_request;
static file_player_finished_cb_t s_finished_callback;
static file_player_cue_track_cb_t s_cue_track_callback;

/* The .cue tracks of the file playing, copied in by file_player_play_cue()
 * while no playback task runs and only read after that. PSRAM: 26 KB of
 * titles has no business in internal SRAM. */
static file_player_cue_t *s_cue;
static atomic_bool s_cue_active = ATOMIC_VAR_INIT(false);
static atomic_int s_cue_track = ATOMIC_VAR_INIT(-1);
/* Where the output has got to in the file, in CD frames - the unit a sheet
 * writes its starts in - so the status can say how far into the track on the
 * air it is, not only into the file. */
static atomic_uint s_position_frames = ATOMIC_VAR_INIT(0);

// What one sample of output takes: the slot is fixed 16-bit stereo.
#define FILE_PLAYER_FRAME_BYTES ((size_t)AUDIO_CHANNEL_COUNT * AUDIO_BITS_PER_SAMPLE / 8U)

void file_player_set_finished_callback(file_player_finished_cb_t callback)
{
    s_finished_callback = callback;
}

void file_player_set_cue_track_callback(file_player_cue_track_cb_t callback)
{
    s_cue_track_callback = callback;
}

static void status_set_state(file_player_state_t state)
{
    taskENTER_CRITICAL(&s_status_lock);
    s_status.state = state;
    taskEXIT_CRITICAL(&s_status_lock);
}

static bool stream_format_for(file_browser_format_t format, radio_stream_format_t *out)
{
    switch (format) {
    case FILE_BROWSER_FORMAT_MP3: *out = RADIO_STREAM_FORMAT_MP3; return true;
    case FILE_BROWSER_FORMAT_AAC: *out = RADIO_STREAM_FORMAT_AAC; return true;
    case FILE_BROWSER_FORMAT_FLAC: *out = RADIO_STREAM_FORMAT_FLAC; return true;
    case FILE_BROWSER_FORMAT_OGG_FLAC: *out = RADIO_STREAM_FORMAT_OGG_FLAC; return true;
    default: return false;
    }
}

static uint8_t *alloc_buffer(size_t size)
{
    // Internal SRAM headroom is tight, so try PSRAM first and keep the
    // internal fallback, matching the radio path.
    uint8_t *buffer = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        buffer = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return buffer;
}

typedef struct {
    FILE *file;
    char *stdio_buffer;
    uint8_t *compressed;
    uint8_t *pcm;
    /* Not a constant, because a FLAC frame is not: the encoder chooses the
     * block size, and a buffer sized for the common 4096 samples rejects every
     * frame of a file written with 4608 - the decoder delivers a frame whole
     * or not at all. Grown to fit once the header says how much it needs. */
    size_t pcm_capacity;
    size_t available;
    size_t offset;
    bool eof;
    bool output_started;
    uint32_t output_sample_rate;
    /* For the position readout. `pcm_bytes` counts what reached the output,
     * not what was read from the drive - the reader runs a whole buffer ahead
     * of the speaker. `header_bytes` is what precedes the audio, so a short
     * file is not reported as longer than it is. */
    uint64_t pcm_bytes;
    uint64_t file_bytes;
    uint64_t header_bytes;
    // How many frames claimed a sample rate the file does not have. Reported
    // once at the end of the track rather than as it happens - see apply_info().
    unsigned int bogus_rate_reports;
    // Set when the length came from the file's own header rather than from a
    // bitrate, which is FLAC. An exact figure must not be replaced by an
    // estimate if the decoder ever starts reporting a rate for it.
    bool length_from_header;
    /* What a jump into the middle of a FLAC needs, and what it is worth
     * carrying 76 bytes for.
     *
     * The decoder cannot be dropped into the middle of a stream. Reset, it
     * waits for a stream to start - magic number, then STREAMINFO - and
     * answers frames with SYNC_NOT_FOUND; left alone, it is somewhere inside a
     * frame that is no longer there. So a jump hands it a stream that begins
     * where the jump landed: these 42 bytes, which are the file's own header
     * with the metadata after STREAMINFO left out, and then audio from a real
     * frame boundary - which `info` is what recognises. */
    flac_streaminfo_t flac_info;
    uint8_t flac_priming[FLAC_SIGNATURE_SIZE + FLAC_BLOCK_HEADER_SIZE + FLAC_STREAMINFO_SIZE];
    bool flac_ready;
    /* Output samples to drop before the next one is played: a jump lands on
     * the frame that contains the target sample, and the samples of that
     * frame before it belong to the track before. */
    uint64_t discard_frames;
    /* Set while the jump to a .cue track's start has not happened yet. An
     * MP3 is aimed by its bitrate, which only the first frame reveals, and
     * that frame is the start of the file - the track before, not this one. */
    bool holding_for_start;
} file_player_context_t;

/* How large a folder cover this will read off the drive.
 *
 * The decoder refuses anything past 600x600 anyway, and a picture that big is
 * comfortably inside a megabyte in either format; past that the file is
 * something else - a scan of the sleeve, a booklet page - and reading it would
 * only delay the first sample to reach the same 96-pixel tile. */
#define FILE_PLAYER_COVER_MAX_BYTES (2048U * 1024U)

/* What the cover on screen currently is, so an album's picture is read off the
 * drive once rather than for every track on it. Reading the 621 KB cover this
 * was built for costs about a second and a half, and it sits between choosing
 * a track and hearing it.
 *
 * Both fields are written only from the player task, in load_tags(). The flag
 * is what makes the path enough to compare: the published cover has to be this
 * folder's picture, not a previous track's own, or a file with a cover of its
 * own would lend it to the next file that has none. */
static char s_folder_cover_directory[FILE_BROWSER_PATH_MAX_LEN];
static bool s_folder_cover_published;
/* Which picture was left on the tile. "Something is on the tile" is not the
 * same question: the rotor and the station icons publish into the same tile,
 * so a cover put up for this folder can have been replaced while another
 * source was playing, and only the generation tells the two apart. */
static unsigned int s_folder_cover_generation;

/* Reads a picture out of a file and publishes it as the cover.
 *
 * The one place either kind of cover is read - the file beside the track, and
 * the block inside it - because both are the same job: a length that has to be
 * worth reading, a buffer that has to come from PSRAM, and bytes the decoders
 * take whole. `what` only names the source in the log.
 */
static bool publish_cover_bytes(FILE *file, size_t offset, size_t length, const char *what)
{
    if (file == NULL || length == 0U) return false;
    if (length > FILE_PLAYER_COVER_MAX_BYTES) {
        ESP_LOGW(TAG, "%s: %u bytes is too large for a cover", what, (unsigned int)length);
        return false;
    }
    if (offset > LONG_MAX || fseek(file, (long)offset, SEEK_SET) != 0) return false;
    uint8_t *picture = alloc_buffer(length);
    if (picture == NULL) {
        ESP_LOGW(TAG, "no memory to read the cover of %s", what);
        return false;
    }
    const size_t read = fread(picture, 1U, length, file);
    const bool published = read == length && album_art_set_image(picture, length);
    if (published) {
        ESP_LOGI(TAG, "cover from %s", what);
    } else {
        ESP_LOGW(TAG, "%s: the cover could not be shown", what);
    }
    free(picture);
    return published;
}

// Fills `out` with the path of the folder's cover picture, if it has one.
static bool find_folder_cover(const char *directory, char *out, size_t out_size)
{
    DIR *dir = opendir(directory);
    if (dir == NULL) return false;
    bool found = false;
    const struct dirent *entry;
    while (!found && (entry = readdir(dir)) != NULL) {
        // FATFS fills d_type, but a directory named cover.png would open and
        // read as nothing; the name test alone is not enough.
        if (entry->d_type == DT_DIR) continue;
        if (!cover_file_name_matches(entry->d_name)) continue;
        found = file_browser_path_child(directory, entry->d_name, out, out_size);
    }
    closedir(dir);
    return found;
}

/* Publishes the picture the folder keeps, for a track whose own tags have
 * none. Returns false when there is nothing to show, which is the ordinary
 * case and not worth a log line of its own.
 *
 * The whole file is read into memory because that is what the decoders take -
 * and it is read again for every track of the album, which sounds wasteful but
 * is not: album_art recognises the same bytes and skips the decode, so the
 * cost is one file read, and the alternative is remembering which directory
 * the last track came from and getting that wrong when the drive changes. */
static bool load_folder_cover(void)
{
    char directory[FILE_BROWSER_PATH_MAX_LEN];
    char path[FILE_BROWSER_PATH_MAX_LEN];
    if (!file_browser_path_parent(s_request.path, directory, sizeof(directory))) return false;
    // Still the same album, and its picture is still the one on screen.
    const album_art_status_t shown = album_art_status();
    if (s_folder_cover_published && shown.present &&
        shown.generation == s_folder_cover_generation &&
        strcmp(directory, s_folder_cover_directory) == 0) {
        return true;
    }
    s_folder_cover_published = false;
    if (!find_folder_cover(directory, path, sizeof(path))) return false;

    FILE *file = fopen(path, "rb");
    if (file == NULL) return false;
    size_t length = 0U;
    if (fseek(file, 0, SEEK_END) == 0) {
        const long end = ftell(file);
        if (end > 0) length = (size_t)end;
    }
    const bool published = publish_cover_bytes(file, 0U, length, path);
    fclose(file);
    if (published) {
        snprintf(s_folder_cover_directory, sizeof(s_folder_cover_directory), "%s", directory);
        s_folder_cover_published = true;
        s_folder_cover_generation = album_art_status().generation;
    }
    return published;
}

/* Walks a FLAC file's metadata blocks for the two numbers the rest of the
 * player cannot work out for itself.
 *
 * MP3 states a bitrate in every frame, and a bitrate plus a file size is a
 * length. FLAC states nothing of the kind: its frames are as large as the
 * music in them needs, so the first frame says nothing about the last, and the
 * player had no length to draw a progress bar from - a FLAC track played with
 * an empty footer where every other format has a position. What FLAC does
 * carry is STREAMINFO, written by the encoder with the exact sample count.
 *
 * The walk also lands on the first audio byte, which is what a jump has to be
 * measured from. Blocks are stepped over rather than read: one of them is the
 * cover, and it is hundreds of kilobytes.
 */
static void read_flac_layout(file_player_context_t *ctx)
{
    uint8_t header[FLAC_BLOCK_HEADER_SIZE];
    uint8_t signature[FLAC_SIGNATURE_SIZE];
    rewind(ctx->file);
    if (fread(signature, 1U, sizeof(signature), ctx->file) != sizeof(signature) ||
        !flac_signature_matches(signature, sizeof(signature))) {
        return;
    }

    flac_streaminfo_t info = {0};
    bool have_info = false;
    bool last = false;
    while (!last && fread(header, 1U, sizeof(header), ctx->file) == sizeof(header)) {
        uint8_t type = 0U;
        size_t block_length = 0U;
        if (!flac_block_header_parse(header, sizeof(header), &last, &type, &block_length)) {
            return;
        }
        if (type == FLAC_BLOCK_STREAMINFO && block_length >= FLAC_STREAMINFO_SIZE) {
            uint8_t block[FLAC_STREAMINFO_SIZE];
            if (fread(block, 1U, sizeof(block), ctx->file) != sizeof(block)) return;
            have_info = flac_streaminfo_parse(block, sizeof(block), &info);
            block_length -= FLAC_STREAMINFO_SIZE;
            if (have_info) {
                // The same block again with the "last metadata block" bit set,
                // so a decoder fed this goes straight from it to the audio.
                memcpy(ctx->flac_priming, signature, FLAC_SIGNATURE_SIZE);
                ctx->flac_priming[4] = 0x80U | FLAC_BLOCK_STREAMINFO;
                ctx->flac_priming[5] = 0x00U;
                ctx->flac_priming[6] = 0x00U;
                ctx->flac_priming[7] = (uint8_t)FLAC_STREAMINFO_SIZE;
                memcpy(ctx->flac_priming + 8, block, sizeof(block));
            }
        }
        if (block_length > 0U && fseek(ctx->file, (long)block_length, SEEK_CUR) != 0) return;
    }

    const long audio_start = ftell(ctx->file);
    if (!have_info || audio_start < 0) return;
    ctx->header_bytes = (uint64_t)audio_start;
    ctx->length_from_header = true;
    ctx->flac_info = info;
    ctx->flac_ready = true;

    const uint32_t seconds =
        file_track_sampled_seconds(info.total_samples, info.sample_rate_hz);
    if (seconds == 0U) return;
    atomic_store_explicit(&s_total_seconds, seconds, memory_order_relaxed);

    /* The average is the only rate a FLAC has. It is what the codec line
     * shows, and what a jump is measured with - the decoder has nothing else
     * to go on, and lands within a frame of where it aimed. */
    const uint16_t bitrate =
        file_track_average_bitrate_kbps(ctx->file_bytes, ctx->header_bytes, seconds);
    taskENTER_CRITICAL(&s_status_lock);
    s_status.bitrate_kbps = bitrate;
    taskEXIT_CRITICAL(&s_status_lock);
    ESP_LOGI(TAG, "track length: %us from %llu samples at %u Hz (header %llu, %u kbps)",
             (unsigned int)seconds, (unsigned long long)info.total_samples,
             (unsigned int)info.sample_rate_hz, (unsigned long long)ctx->header_bytes,
             (unsigned int)bitrate);
}

/* Reads the tags before a single frame is decoded, so the screen is right from
 * the moment the track appears on it rather than a second into playback. It
 * costs one read of the tag - 68 KB on the album this was built for - and a
 * JPEG decode, both of which finish long before the first sample is due. */
static void load_tags(file_player_context_t *ctx)
{
    uint8_t *scratch = alloc_buffer(AUDIO_TAGS_SCRATCH_SIZE);
    audio_tags_t tags;
    audio_tags_clear(&tags);
    if (scratch == NULL) {
        ESP_LOGW(TAG, "no memory to read tags");
    } else {
        (void)audio_tags_read_file(ctx->file, scratch, AUDIO_TAGS_SCRATCH_SIZE, &tags);
    }
    // The tag reader seeks about the file; the decoder needs it back at zero.
    rewind(ctx->file);

    if (audio_tags_have_text(&tags)) {
        taskENTER_CRITICAL(&s_status_lock);
        s_tags = tags;
        taskEXIT_CRITICAL(&s_status_lock);
        atomic_fetch_add_explicit(&s_tags_revision, 1U, memory_order_release);
        ESP_LOGI(TAG, "tags: title=\"%s\" artist=\"%s\" album=\"%s\"", tags.title,
                 tags.artist, tags.album);
    }

    /* The track's own picture wins: it belongs to this file, while the one in
     * the folder belongs to whatever else is filed with it. The folder is
     * tried when the tag has no picture - and also when it has one that will
     * not decode, because from the listener's side those are the same thing,
     * and the placeholder is the worse answer of the two.
     *
     * When neither turns anything up the cover is cleared rather than left
     * alone: the previous track's picture over this one is worse than none. */
    const bool readable = tags.picture_format == AUDIO_TAGS_PICTURE_JPEG ||
                          tags.picture_format == AUDIO_TAGS_PICTURE_PNG;
    bool embedded = false;
    if (readable && scratch != NULL && tags.picture_length > 0U) {
        embedded = album_art_set_image(scratch + tags.picture_offset, tags.picture_length);
    } else if (readable && tags.picture_file_length > 0U) {
        /* Too large to have been kept in the tag buffer - 140 KB of cover in a
         * file read through 128 KB is an ordinary album, not a strange one -
         * so it is read on its own, exactly the picture and nothing else. The
         * reader knows where it lies without having read it. */
        embedded = publish_cover_bytes(ctx->file, tags.picture_file_offset,
                                       tags.picture_file_length, s_request.path);
    }
    if (embedded) {
        // The tile now holds this track's own picture, so the next track in
        // this folder cannot assume the folder's is still up.
        s_folder_cover_published = false;
    } else if (!load_folder_cover()) {
        album_art_clear();
        s_folder_cover_published = false;
    }
    free(scratch);
}

static bool refill(file_player_context_t *ctx)
{
    if (ctx->offset > 0U) {
        // Files are cheap to read, so compact on every refill rather than
        // tracking a lazy read cursor the way the network path has to.
        memmove(ctx->compressed, ctx->compressed + ctx->offset, ctx->available);
        ctx->offset = 0U;
    }
    const size_t room = FILE_PLAYER_INPUT_SIZE - ctx->available;
    if (room == 0U) return false;
    const size_t want = room < FILE_PLAYER_READ_CHUNK ? room : FILE_PLAYER_READ_CHUNK;
    const size_t received = fread(ctx->compressed + ctx->available, 1U, want, ctx->file);
    ctx->available += received;
    if (received < want) ctx->eof = true;
    return received > 0U;
}

static esp_err_t apply_info(file_player_context_t *ctx, const radio_decoder_info_t *info)
{
    /* Once the output is running, its rate is the file's rate: a frame that
     * disagrees is one the decoder mis-synced on, not a change.
     *
     * The decoder resyncs on any byte pattern that looks like a frame header,
     * and inside 320 kbps audio that happens often enough to matter. On one
     * perfectly uniform file - 5172 frames, every one MPEG1 44.1 kHz - it
     * claimed 8000, 22050, 32000 and 48000 Hz across 42 frames of a
     * three-minute track. Each claim stopped the I2S channel and started it
     * again behind a 93 ms silent pre-roll, which is what the bubbling was,
     * and a pause landing in that window was undone by the restart.
     *
     * Asking for the same rate twice in a row does not filter this: a
     * mis-synced frame is announced once as a header and once again with its
     * PCM, so it agrees with itself. Locking is what works, and it costs
     * nothing real - a file has one sample rate. A concatenated one would play
     * its second half at the first half's speed, which is a trade worth making
     * against a clean file bubbling forty times.
     *
     * The whole of `info` is dropped, not only the rate: such a frame's
     * bitrate is invented too, and that is what the track length is computed
     * from. */
    if (ctx->output_started && info->sample_rate != 0U &&
        info->sample_rate != ctx->output_sample_rate) {
        ++ctx->bogus_rate_reports;
        return ESP_OK;
    }

    taskENTER_CRITICAL(&s_status_lock);
    if (info->bitrate_kbps > 0U) s_status.bitrate_kbps = (uint16_t)info->bitrate_kbps;
    if (info->sample_rate > 0U) s_status.sample_rate_hz = info->sample_rate;
    taskEXIT_CRITICAL(&s_status_lock);
    // The first frame is what reveals the bitrate, and the bitrate is what
    // turns a file size into a duration - for the formats that have no better
    // answer of their own.
    if (info->bitrate_kbps > 0U && !ctx->length_from_header) {
        const uint32_t total = file_track_total_seconds(ctx->file_bytes, ctx->header_bytes,
                                                        (uint16_t)info->bitrate_kbps);
        const uint32_t previous =
            atomic_exchange_explicit(&s_total_seconds, total, memory_order_relaxed);
        // Once per track, when the length first becomes known: it is the only
        // place the estimate can be checked against the file it came from.
        if (previous == 0U && total > 0U) {
            ESP_LOGI(TAG, "track length: %us from %llu bytes at %u kbps (header %llu)",
                     (unsigned int)total, (unsigned long long)ctx->file_bytes,
                     (unsigned int)info->bitrate_kbps,
                     (unsigned long long)ctx->header_bytes);
        }
    }

    if (info->sample_rate == 0U || info->sample_rate == ctx->output_sample_rate) {
        return ESP_OK;
    }
    // Changing rate under a running I2S channel keeps the old clock for the
    // already-queued DMA buffers, so the tail of the previous track plays back
    // at the wrong speed. Disable, retune, and let the next PCM block restart
    // output with its silent pre-roll.
    esp_err_t result = ESP_OK;
    if (ctx->output_started) {
        result = board_audio_set_enabled(false);
        if (result == ESP_OK) ctx->output_started = false;
    }
    if (result == ESP_OK) result = board_audio_set_sample_rate(info->sample_rate);
    if (result == ESP_OK) ctx->output_sample_rate = info->sample_rate;
    return result;
}

/* The rate positions are counted at. Before the first frame has set up the
 * output that is the file's own, which only a FLAC states up front. */
static uint32_t position_rate(const file_player_context_t *ctx)
{
    if (ctx->output_sample_rate != 0U) return ctx->output_sample_rate;
    return ctx->flac_ready ? ctx->flac_info.sample_rate_hz : 0U;
}

/* A .cue track's text in place of the file's tags: a disc side ripped as one
 * file is tagged, if at all, as the side - the sheet is what knows the song. */
static void publish_cue_tags(size_t track)
{
    if (s_cue == NULL || track >= s_cue->count) return;
    taskENTER_CRITICAL(&s_status_lock);
    snprintf(s_tags.title, sizeof(s_tags.title), "%s", s_cue->title[track]);
    snprintf(s_tags.artist, sizeof(s_tags.artist), "%s", s_cue->performer[track]);
    snprintf(s_tags.album, sizeof(s_tags.album), "%s", s_cue->album);
    taskEXIT_CRITICAL(&s_status_lock);
    atomic_fetch_add_explicit(&s_tags_revision, 1U, memory_order_release);
}

/* Publishes where the output is and, on a .cue file, which track that is -
 * and when it has become another one, says so. The file goes on playing
 * underneath: this is how a disc side is played straight through, with no
 * gap at a track boundary, and still named track by track. */
static void track_position(file_player_context_t *ctx)
{
    const uint32_t rate = position_rate(ctx);
    if (rate == 0U) return;
    const uint64_t samples = ctx->pcm_bytes / FILE_PLAYER_FRAME_BYTES;
    atomic_store_explicit(&s_elapsed_seconds, (uint32_t)(samples / rate), memory_order_relaxed);
    atomic_store_explicit(&s_position_frames, (uint32_t)(samples * 75U / rate),
                          memory_order_relaxed);
    if (!atomic_load_explicit(&s_cue_active, memory_order_acquire) || ctx->holding_for_start) {
        return;
    }
    const int track =
        (int)file_track_cue_track_at(s_cue->start_frames, s_cue->count, samples, rate);
    const int previous = atomic_exchange_explicit(&s_cue_track, track, memory_order_acq_rel);
    if (previous == track) return;
    publish_cue_tags((size_t)track);
    ESP_LOGI(TAG, "cue track %d: \"%s\"", track + 1, s_cue->title[track]);
    const file_player_cue_track_cb_t callback = s_cue_track_callback;
    if (callback != NULL) callback((size_t)track);
}

static esp_err_t write_pcm(file_player_context_t *ctx, const uint8_t *pcm, size_t length)
{
    /* A pause that arrives between the decoder producing this block and the
     * write below would otherwise be undone here: starting the output turns
     * the DAC back on and reports PLAYING again, and the loop only tests the
     * pause flag on its next pass. Dropping the block costs 26 ms that nobody
     * was going to hear - the user has just pressed pause. */
    if (atomic_load_explicit(&s_paused, memory_order_acquire)) return ESP_OK;
    // The start of the file, while the jump to the track's start waits for a
    // bitrate to aim with: not the track that was asked for.
    if (ctx->holding_for_start) return ESP_OK;
    if (ctx->discard_frames > 0U) {
        const size_t frames = length / FILE_PLAYER_FRAME_BYTES;
        const size_t drop =
            ctx->discard_frames < frames ? (size_t)ctx->discard_frames : frames;
        ctx->discard_frames -= drop;
        /* Not counted: the jump already set the position to the target, and
         * these are the samples before it. Setting it to where the frame
         * started instead would name the track before for a frame's time. */
        pcm += drop * FILE_PLAYER_FRAME_BYTES;
        length -= drop * FILE_PLAYER_FRAME_BYTES;
        if (length == 0U) return ESP_OK;
    }

    size_t written = 0U;
    esp_err_t result;
    if (!ctx->output_started) {
        result = board_audio_start(pcm, length, &written);
        if (result == ESP_OK && written < length) {
            size_t remainder = 0U;
            result = board_audio_write(pcm + written, length - written, &remainder, 1000U);
            written += remainder;
        }
        if (result == ESP_OK) {
            ctx->output_started = true;
            status_set_state(FILE_PLAYER_STATE_PLAYING);
            ESP_LOGI(TAG, "PCM output started: rate=%u block=%u",
                     (unsigned int)ctx->output_sample_rate, (unsigned int)length);
        }
    } else {
        result = board_audio_write(pcm, length, &written, 1000U);
    }
    if (result != ESP_OK || written != length) {
        ESP_LOGE(TAG, "PCM output failed: err=%s written=%u expected=%u",
                 esp_err_to_name(result), (unsigned int)written, (unsigned int)length);
        return ESP_FAIL;
    }
    ctx->pcm_bytes += written;
    // The output slot is fixed 16-bit stereo whatever the file was, so the
    // board's own format converts these bytes to time - not the file's.
    track_position(ctx);
    return ESP_OK;
}

static bool should_stop(void)
{
    return atomic_load_explicit(&s_stop_requested, memory_order_acquire);
}

// True while paused, after sleeping; keeps the two playback loops from each
// growing their own copy of the pause handling.
static bool wait_while_paused(void)
{
    if (!atomic_load_explicit(&s_paused, memory_order_acquire)) return false;
    vTaskDelay(pdMS_TO_TICKS(20));
    return true;
}

/* Takes the pending jump, if there is one. FILE_PLAYER_SEEK_NONE means there is
 * not; the exchange makes sure one request is acted on once, even if a second
 * arrives while this one is being applied. */
static uint32_t take_seek_request(void)
{
    return atomic_exchange_explicit(&s_seek_request, FILE_PLAYER_SEEK_NONE,
                                    memory_order_acq_rel);
}

/* Moves the position counters to sample `samples` after the file has been
 * repositioned. The elapsed reading is published here rather than waiting for
 * the next block, so the screen shows the new position from the moment the
 * jump lands instead of falling back to the old one for a poll or two. */
static void seek_reset_position(file_player_context_t *ctx, uint64_t samples)
{
    ctx->pcm_bytes = samples * FILE_PLAYER_FRAME_BYTES;
    track_position(ctx);
}

/* How far past the estimate to look for a frame to start on.
 *
 * The estimate comes from an average bitrate, so it lands inside a frame
 * rather than on one, and the next boundary is at most one frame away - which
 * at 4096 samples and this album's rate is about ten kilobytes. Two frames'
 * worth of room, and it has never needed a tenth of it. */
#define FILE_PLAYER_FLAC_SYNC_WINDOW 32768U

/* Finds the first frame at or after `*offset`, moves `*offset` onto it and
 * says which sample it starts at.
 *
 * Returns the file to where it was and reports failure if there is no frame
 * header to be found, which leaves the caller free to abandon the jump rather
 * than feed the decoder a position it cannot start from. */
static bool flac_frame_at(file_player_context_t *ctx, uint64_t *offset, uint64_t *first_sample)
{
    const long previous = ftell(ctx->file);
    if (*offset > LONG_MAX || fseek(ctx->file, (long)*offset, SEEK_SET) != 0) return false;
    const size_t window = FILE_PLAYER_INPUT_SIZE < FILE_PLAYER_FLAC_SYNC_WINDOW
                              ? FILE_PLAYER_INPUT_SIZE
                              : FILE_PLAYER_FLAC_SYNC_WINDOW;
    // Read into the input buffer rather than a second one: it is about to be
    // emptied for the jump anyway.
    const size_t read = fread(ctx->compressed, 1U, window, ctx->file);
    size_t found = 0U;
    flac_frame_header_t header;
    const bool aligned =
        read > 0U && flac_frame_find_sync(ctx->compressed, read, &ctx->flac_info, &found) &&
        flac_frame_header_parse(ctx->compressed + found, read - found, &header);
    if (aligned) {
        *offset += found;
        *first_sample = flac_frame_first_sample(&header, &ctx->flac_info);
    } else if (previous >= 0) {
        (void)fseek(ctx->file, previous, SEEK_SET);
    }
    return aligned;
}

/* Lands a FLAC jump on the frame that holds sample `target`.
 *
 * Aimed by the file's own sample count, then corrected by what the frame
 * header says: a frame past the target means stepping back, one far short of
 * it stepping on, so what is left to drop before the target is under a few
 * frames. Six tries; in practice the second lands. `*landed` is the first
 * sample of the frame chosen. */
#define FILE_PLAYER_FLAC_AIM_TRIES 6U
static bool flac_aim(file_player_context_t *ctx, uint64_t target, uint16_t bitrate_kbps,
                     uint64_t *offset, uint64_t *landed)
{
    const flac_streaminfo_t *info = &ctx->flac_info;
    const uint32_t block = info->max_block_size != 0U ? info->max_block_size : 4096U;
    uint64_t aim =
        info->total_samples != 0U
            ? file_track_flac_aim(ctx->header_bytes, ctx->file_bytes, info->total_samples, target)
            : file_track_seek_offset(ctx->file_bytes, ctx->header_bytes, bitrate_kbps,
                                     (uint32_t)(target / info->sample_rate_hz));
    bool found = false;
    for (unsigned int attempt = 0U; attempt < FILE_PLAYER_FLAC_AIM_TRIES; ++attempt) {
        uint64_t at = aim;
        uint64_t first = 0U;
        if (!flac_frame_at(ctx, &at, &first)) break;
        found = true;
        *offset = at;
        *landed = first;
        if (info->total_samples == 0U) break;  // nothing to correct by
        if (first > target) {
            aim = file_track_flac_back_off(at, ctx->header_bytes, ctx->file_bytes,
                                           info->total_samples, first - target, block);
        } else if (target - first > 4U * (uint64_t)block) {
            // Short by more than a few frames: step on by the gap, less a block.
            aim = at + (file_track_flac_aim(0U, ctx->file_bytes - ctx->header_bytes,
                                            info->total_samples, target - first - block));
        } else {
            break;
        }
    }
    return found && *landed <= target;
}

/* Applies a pending jump on the compressed path. The target is in CD frames
 * from the start of the file - the unit a .cue sheet writes, fine enough for
 * a scrub bar too.
 *
 * The decoder has to be reset, not just fed from the new offset: landing
 * mid-frame leaves it holding a partial frame and, for MP3, a bit reservoir
 * that refers to bytes that are now behind the read head - the same state that
 * produced the silent stall this player was already taught to recover from.
 * After the reset it resyncs on the first frame header it finds, which costs
 * at most one frame of audio.
 *
 * A failed seek is not a failed track: the file is left where it was and
 * playback carries on from there. */
static void apply_seek(file_player_context_t *ctx, radio_decoder_t *decoder)
{
    if (atomic_load_explicit(&s_seek_request, memory_order_acquire) == FILE_PLAYER_SEEK_NONE) {
        return;
    }
    uint16_t bitrate_kbps;
    taskENTER_CRITICAL(&s_status_lock);
    bitrate_kbps = s_status.bitrate_kbps;
    taskEXIT_CRITICAL(&s_status_lock);
    const uint32_t rate = position_rate(ctx);
    /* Anything but a FLAC is aimed by its bitrate, which the first frame is
     * what reveals: a jump asked for before then - the start of a .cue track,
     * set before the first byte was read - waits for it. */
    if (!ctx->flac_ready && (bitrate_kbps == 0U || rate == 0U)) return;
    const uint32_t target = take_seek_request();
    if (target == FILE_PLAYER_SEEK_NONE) return;

    const uint64_t samples = file_track_cd_frames_to_samples(target, rate);
    const long previous = ftell(ctx->file);
    uint64_t offset = 0U;
    uint64_t landed = samples;
    // Where the output is to stand after the jump: the target, or - when the
    // jump could not be made - the frame the decoder carries on from.
    uint64_t goal = samples;
    if (ctx->flac_ready) {
        // Past the last sample: the track is over, and ends the way it would
        // have - a .cue side then goes on to the next one.
        if (ctx->flac_info.total_samples != 0U && samples >= ctx->flac_info.total_samples) {
            ctx->available = 0U;
            ctx->offset = 0U;
            ctx->eof = true;
            ctx->holding_for_start = false;
            ESP_LOGI(TAG, "seek past the end of the file; ending the track");
            return;
        }
        if (!flac_aim(ctx, samples, bitrate_kbps, &offset, &landed)) {
            /* Staying put is not free: the search read its windows into the
             * input buffer, so what the decoder had there is gone. It starts
             * again from the frame at the first byte it had not consumed. */
            const uint64_t unread =
                previous >= 0 && (uint64_t)previous > ctx->available
                    ? (uint64_t)previous - ctx->available
                    : ctx->header_bytes;
            offset = unread;
            if (!flac_frame_at(ctx, &offset, &landed)) {
                ESP_LOGW(TAG, "lost the stream after a failed jump; ending the track");
                ctx->available = 0U;
                ctx->offset = 0U;
                ctx->eof = true;
                ctx->holding_for_start = false;
                return;
            }
            ESP_LOGW(TAG, "no frame to start from before sample %llu; staying put",
                     (unsigned long long)samples);
            goal = landed;
        }
    } else {
        offset = file_track_seek_offset(ctx->file_bytes, ctx->header_bytes, bitrate_kbps,
                                        target / 75U);
    }
    if (offset > LONG_MAX || fseek(ctx->file, (long)offset, SEEK_SET) != 0) {
        ESP_LOGW(TAG, "cannot seek to frame %u (offset %llu)", (unsigned int)target,
                 (unsigned long long)offset);
        ctx->holding_for_start = false;
        return;
    }
    // Everything already read belongs to the old position, including the
    // end-of-file mark: a seek backwards from the last block has more to read.
    ctx->available = 0U;
    ctx->offset = 0U;
    ctx->eof = false;
    radio_decoder_reset(decoder);
    if (ctx->flac_ready) {
        // The decoder starts again on a stream that begins here; MP3 needs no
        // such thing, since one of its frames says everything about itself.
        memcpy(ctx->compressed, ctx->flac_priming, sizeof(ctx->flac_priming));
        ctx->available = sizeof(ctx->flac_priming);
    }
    ctx->discard_frames = goal - landed;
    ctx->holding_for_start = false;
    seek_reset_position(ctx, goal);
    ESP_LOGI(TAG, "seek to %.2fs: offset=%llu of %llu, frame at sample %llu, dropping %llu",
             (double)target / 75.0, (unsigned long long)offset,
             (unsigned long long)ctx->file_bytes, (unsigned long long)landed,
             (unsigned long long)ctx->discard_frames);
}

// WAV carries finished PCM, so there is no decoder in this path at all - only
// the header parse, an optional mono-to-stereo expansion, and I2S.
static bool play_wav(file_player_context_t *ctx, unsigned int *pcm_blocks)
{
    file_wav_info_t wav = {0};
    file_wav_result_t parsed = file_wav_parse_header(ctx->compressed, ctx->available, &wav);
    while (parsed == FILE_WAV_NEED_MORE_DATA && !ctx->eof) {
        if (!refill(ctx) && ctx->available == FILE_PLAYER_INPUT_SIZE) break;
        parsed = file_wav_parse_header(ctx->compressed, ctx->available, &wav);
    }
    if (parsed != FILE_WAV_OK) {
        ESP_LOGE(TAG, "unusable WAV header: result=%d", (int)parsed);
        return true;
    }
    ESP_LOGI(TAG, "WAV ready: rate=%u channels=%u bits=%u data=%u bytes",
             (unsigned int)wav.sample_rate, (unsigned int)wav.channels,
             (unsigned int)wav.bits_per_sample, (unsigned int)wav.data_length);

    const radio_decoder_info_t info = {
        .sample_rate = wav.sample_rate,
        .channels = (uint8_t)wav.channels,
        .bits_per_sample = (uint8_t)wav.bits_per_sample,
        .bitrate_kbps = wav.sample_rate * wav.channels * wav.bits_per_sample / 1000U,
    };
    if (apply_info(ctx, &info) != ESP_OK) {
        ESP_LOGE(TAG, "cannot configure output for %u Hz", (unsigned int)wav.sample_rate);
        return true;
    }
    // Known exactly here, unlike a compressed file where a leading ID3 tag is
    // simply counted as audio and costs a percent or so on the estimate.
    ctx->header_bytes = wav.data_offset;
    if (fseek(ctx->file, (long)wav.data_offset, SEEK_SET) != 0) {
        ESP_LOGE(TAG, "cannot seek to the WAV sample data");
        return true;
    }

    const size_t frame_bytes = (size_t)wav.channels * 2U;
    // A declared length of zero means the writer never finished the header, so
    // play to the end of the file instead of stopping immediately.
    size_t remaining = wav.data_length > 0U ? wav.data_length : SIZE_MAX;
    // Exact here, unlike the compressed path: PCM has a fixed number of bytes
    // per sample, so a WAV seek lands on the sample that was asked for.
    for (;;) {
        if (should_stop()) return false;
        if (wait_while_paused()) continue;

        const uint32_t target = take_seek_request();
        if (target != FILE_PLAYER_SEEK_NONE) {
            const uint64_t sample = file_track_cd_frames_to_samples(target, wav.sample_rate);
            uint64_t skip = sample * frame_bytes;
            const uint64_t audio_bytes =
                wav.data_length > 0U ? (uint64_t)wav.data_length : ctx->file_bytes;
            if (skip > audio_bytes) skip = audio_bytes - (audio_bytes % frame_bytes);
            const uint64_t offset = (uint64_t)wav.data_offset + skip;
            if (offset > LONG_MAX || fseek(ctx->file, (long)offset, SEEK_SET) != 0) {
                ESP_LOGW(TAG, "cannot seek to frame %u in the WAV data", (unsigned int)target);
            } else {
                remaining = wav.data_length > 0U ? (size_t)(audio_bytes - skip) : SIZE_MAX;
                seek_reset_position(ctx, skip / frame_bytes);
                ESP_LOGI(TAG, "seek to %.2fs: offset=%llu", (double)target / 75.0,
                         (unsigned long long)offset);
            }
        }

        size_t want = FILE_PLAYER_READ_CHUNK < remaining ? FILE_PLAYER_READ_CHUNK : remaining;
        want -= want % frame_bytes;
        if (want == 0U) return false;
        const size_t received = fread(ctx->compressed, 1U, want, ctx->file);
        // A trailing partial frame is truncation in the file, not an error
        // worth failing the track over.
        const size_t usable = received - (received % frame_bytes);
        if (usable == 0U) return false;
        remaining -= usable;

        const uint8_t *out = ctx->compressed;
        size_t out_length = usable;
        if (wav.channels == 1U) {
            // The I2S channel is fixed at 16-bit stereo slots, so a mono file
            // has to be duplicated into both channels rather than reconfigured.
            if (!audio_pcm_to_stereo_s16(ctx->compressed, usable, 1U, ctx->pcm,
                                         ctx->pcm_capacity, &out_length)) {
                ESP_LOGE(TAG, "cannot expand %u mono bytes to stereo",
                         (unsigned int)usable);
                return true;
            }
            out = ctx->pcm;
        }
        if (write_pcm(ctx, out, out_length) != ESP_OK) return true;
        ++(*pcm_blocks);
        if (received < want) return false;
    }
}

/* Makes room for a whole decoded frame, now that the header has said how big
 * one is. Returns false only when there is no memory for it, which ends the
 * track - a frame that does not fit is never delivered at all, so playing on
 * would mean playing silence. */
static bool grow_pcm_buffer(file_player_context_t *ctx, const radio_decoder_info_t *info)
{
    if (info->pcm_frame_bytes <= ctx->pcm_capacity) return true;
    uint8_t *grown = alloc_buffer(info->pcm_frame_bytes);
    if (grown == NULL) {
        ESP_LOGE(TAG, "no memory for a %u byte frame", (unsigned int)info->pcm_frame_bytes);
        return false;
    }
    ESP_LOGI(TAG, "output buffer grown from %u to %u bytes for this file",
             (unsigned int)ctx->pcm_capacity, (unsigned int)info->pcm_frame_bytes);
    free(ctx->pcm);
    ctx->pcm = grown;
    ctx->pcm_capacity = info->pcm_frame_bytes;
    return true;
}

// One track, start to finish. The task below is created for it and exits
// with it.
static void file_player_run_track(void)
{
    file_player_context_t ctx = {0};
    // Cleared before anything can read them, so the previous track's position
    // is never shown against this one.
    atomic_store_explicit(&s_elapsed_seconds, 0U, memory_order_relaxed);
    atomic_store_explicit(&s_total_seconds, 0U, memory_order_relaxed);
    // A jump asked for a moment before the track changed belongs to the track
    // that is gone; applying it to this one would open it in the middle.
    atomic_store_explicit(&s_seek_request, FILE_PLAYER_SEEK_NONE, memory_order_relaxed);
    atomic_store_explicit(&s_position_frames, 0U, memory_order_relaxed);
    const bool cue = atomic_load_explicit(&s_cue_active, memory_order_acquire);
    atomic_store_explicit(&s_cue_track, cue ? (int)s_cue->first : -1, memory_order_release);
    /* A .cue track that does not start the file is reached by a jump before
     * the first sample is played, the same jump the scrub bar makes. */
    if (cue && s_cue->start_frames[s_cue->first] > 0U) {
        atomic_store_explicit(&s_seek_request, s_cue->start_frames[s_cue->first],
                              memory_order_release);
        ctx.holding_for_start = true;
    }
    radio_decoder_t *decoder = NULL;
    radio_stream_format_t stream_format = RADIO_STREAM_FORMAT_MP3;
    const bool is_wav = s_request.format == FILE_BROWSER_FORMAT_WAV;
    bool failed = false;

    if (!is_wav && !stream_format_for(s_request.format, &stream_format)) {
        ESP_LOGE(TAG, "no decoder for format %d", (int)s_request.format);
        failed = true;
    }

    if (!failed) {
        ctx.compressed = alloc_buffer(FILE_PLAYER_INPUT_SIZE);
        ctx.pcm = alloc_buffer(FILE_PLAYER_PCM_SIZE);
        ctx.pcm_capacity = ctx.pcm != NULL ? FILE_PLAYER_PCM_SIZE : 0U;
        ctx.file = fopen(s_request.path, "rb");
        // Before any read, which is the only time setvbuf may be called.
        // PSRAM only: without it the file still reads, just slower.
        ctx.stdio_buffer = ctx.file != NULL ? heap_caps_malloc(FILE_PLAYER_STDIO_BUFFER,
                                                               MALLOC_CAP_SPIRAM)
                                            : NULL;
        if (ctx.stdio_buffer != NULL &&
            setvbuf(ctx.file, ctx.stdio_buffer, _IOFBF, FILE_PLAYER_STDIO_BUFFER) != 0) {
            free(ctx.stdio_buffer);
            ctx.stdio_buffer = NULL;
        }
        if (ctx.file != NULL && fseek(ctx.file, 0, SEEK_END) == 0) {
            const long end = ftell(ctx.file);
            if (end > 0) ctx.file_bytes = (uint64_t)end;
            rewind(ctx.file);
        }
        if (!is_wav) decoder = radio_decoder_create(stream_format);
        if (ctx.compressed == NULL || ctx.pcm == NULL || (!is_wav && decoder == NULL)) {
            ESP_LOGE(TAG, "allocation failed for %s", s_request.path);
            failed = true;
        } else if (ctx.file == NULL) {
            ESP_LOGE(TAG, "cannot open %s", s_request.path);
            failed = true;
        }
    }

    if (!failed) {
        taskENTER_CRITICAL(&s_status_lock);
        snprintf(s_status.codec, sizeof(s_status.codec), "%s",
                 is_wav ? "WAV" : radio_stream_format_codec_name(stream_format));
        taskEXIT_CRITICAL(&s_status_lock);
        load_tags(&ctx);
        // The sheet names the track; the file's own tags name the side.
        if (cue) publish_cue_tags(s_cue->first);
        if (s_request.format == FILE_BROWSER_FORMAT_FLAC) read_flac_layout(&ctx);
        // Whatever the reads above left behind: the decoder starts at zero.
        rewind(ctx.file);
        ESP_LOGI(TAG, "playing %s", s_request.path);
    }

    unsigned int pcm_blocks = 0U;
    if (!failed && is_wav) {
        // WAV goes from the file to I2S a chunk at a time with no backlog in
        // between, so there is no fill to report - and 0 would read as starved.
        atomic_store_explicit(&s_input_fill_percent, RADIO_PREBUFFER_PERCENT_NONE,
                              memory_order_relaxed);
        failed = play_wav(&ctx, &pcm_blocks);
    }
    while (!failed && !is_wav) {
        if (should_stop()) break;
        if (wait_while_paused()) continue;
        // After the pause check, so a jump requested while the file is paused
        // for scrubbing is applied on the pass that resumes it rather than
        // being swallowed by the pause loop.
        apply_seek(&ctx, decoder);

        bool need_input = ctx.available == 0U;
        atomic_store_explicit(&s_input_fill_percent,
                              radio_prebuffer_percent(ctx.available, FILE_PLAYER_INPUT_SIZE),
                              memory_order_relaxed);
        if (!need_input) {
            size_t consumed = 0U;
            size_t pcm_bytes = 0U;
            radio_decoder_info_t info = {0};
            const radio_decoder_result_t result =
                radio_decoder_decode(decoder, ctx.compressed + ctx.offset, ctx.available,
                                     ctx.pcm, ctx.pcm_capacity, &consumed, &pcm_bytes,
                                     &info);
            if (consumed > ctx.available) {
                ESP_LOGE(TAG, "decoder consumed %u of %u available", (unsigned int)consumed,
                         (unsigned int)ctx.available);
                failed = true;
                break;
            }
            ctx.available -= consumed;
            ctx.offset += consumed;

            if (result == RADIO_DECODER_HEADER_READY || result == RADIO_DECODER_PCM_READY) {
                if (apply_info(&ctx, &info) != ESP_OK) {
                    ESP_LOGE(TAG, "cannot configure output for %u Hz",
                             (unsigned int)info.sample_rate);
                    failed = true;
                    break;
                }
                if (result == RADIO_DECODER_HEADER_READY && !grow_pcm_buffer(&ctx, &info)) {
                    failed = true;
                    break;
                }
                if (result == RADIO_DECODER_HEADER_READY) {
                    ESP_LOGI(TAG, "decoder ready: codec=%s rate=%u channels=%u bits=%u",
                             radio_stream_format_codec_name(stream_format),
                             (unsigned int)info.sample_rate, (unsigned int)info.channels,
                             (unsigned int)info.bits_per_sample);
                } else if (pcm_bytes > 0U) {
                    if (write_pcm(&ctx, ctx.pcm, pcm_bytes) != ESP_OK) {
                        failed = true;
                        break;
                    }
                    ++pcm_blocks;
                }
            } else if (result == RADIO_DECODER_NEED_MORE_DATA) {
                need_input = true;
            } else if (result == RADIO_DECODER_END_OF_STREAM) {
                break;
            } else {
                ESP_LOGE(TAG, "decoder error: result=%d available=%u consumed=%u", (int)result,
                         (unsigned int)ctx.available, (unsigned int)consumed);
                failed = true;
                break;
            }
        }

        if (need_input) {
            // The decoder wants more but the file is spent. Whatever is left
            // in the buffer is a partial frame or a trailing tag, so this is a
            // normal end of track rather than a corrupt file.
            if (ctx.eof) break;
            if (!refill(&ctx) && ctx.available == FILE_PLAYER_INPUT_SIZE) {
                ESP_LOGE(TAG, "compressed frame larger than the %u byte input buffer",
                         (unsigned int)FILE_PLAYER_INPUT_SIZE);
                failed = true;
                break;
            }
        }
    }

    if (ctx.output_started) (void)board_audio_set_enabled(false);
    // Nothing is buffered once the task is gone; leaving the last reading in
    // place would show a healthy buffer on a stopped player.
    atomic_store_explicit(&s_input_fill_percent, 0U, memory_order_relaxed);
    if (decoder != NULL) radio_decoder_destroy(decoder);
    if (ctx.file != NULL) fclose(ctx.file);
    // Only after the fclose: the FILE reads into it until then.
    free(ctx.stdio_buffer);
    free(ctx.compressed);
    free(ctx.pcm);

    const bool ended_by_itself = !failed && !should_stop();
    ESP_LOGI(TAG, "playback finished: blocks=%u failed=%d natural_end=%d bogus_rates=%u",
             pcm_blocks, (int)failed, (int)ended_by_itself, ctx.bogus_rate_reports);
    status_set_state(failed ? FILE_PLAYER_STATE_ERROR : FILE_PLAYER_STATE_STOPPED);
    // Clear the running flag and the handle before notifying, so the listener
    // can start the next track without waiting out this task's stop timeout.
    atomic_store_explicit(&s_task_running, false, memory_order_release);
    const file_player_finished_cb_t callback = s_finished_callback;
    if (ended_by_itself && callback != NULL) callback();
}

static void file_player_task(void *arg)
{
    (void)arg;
    file_player_run_track();
    vTaskDelete(NULL);
}

/* Waits for the worker to finish whatever track it is on. False means it is
 * still going after FILE_PLAYER_STOP_TIMEOUT_MS, and the stop request is
 * deliberately left standing in that case: clearing it would withdraw the only
 * instruction the worker has to stop, and the caller would then hand it a
 * second track on top of the first. */
static bool wait_for_track_end(void)
{
    atomic_store_explicit(&s_stop_requested, true, memory_order_release);
    // Unpause too: a paused task never reaches the stop check otherwise.
    atomic_store_explicit(&s_paused, false, memory_order_release);
    for (unsigned int waited = 0U;
         atomic_load_explicit(&s_task_running, memory_order_acquire) &&
         waited < FILE_PLAYER_STOP_TIMEOUT_MS;
         waited += 10U) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (atomic_load_explicit(&s_task_running, memory_order_acquire)) {
        ESP_LOGE(TAG, "playback task still running after %ums",
                 (unsigned int)FILE_PLAYER_STOP_TIMEOUT_MS);
        return false;
    }
    atomic_store_explicit(&s_stop_requested, false, memory_order_release);
    return true;
}

esp_err_t file_player_init(void)
{
    if (s_control_lock != NULL) return ESP_OK;
    s_control_lock = xSemaphoreCreateMutex();
    if (s_control_lock == NULL) return ESP_ERR_NO_MEM;
    // Owned here because this is what fills it; the screen only ever reads.
    const esp_err_t art = album_art_init();
    if (art != ESP_OK) return art;
    memset(&s_status, 0, sizeof(s_status));
    // PSRAM only - it is 26 KB of titles; without it a sheet plays as files.
    s_cue = heap_caps_calloc(1U, sizeof(*s_cue), MALLOC_CAP_SPIRAM);
    if (s_cue == NULL) ESP_LOGW(TAG, "no memory for .cue playback");
    return ESP_OK;
}

static esp_err_t start_track(const char *path, const char *display_name,
                             file_browser_format_t format, const file_player_cue_t *cue)
{
    if (path == NULL || s_control_lock == NULL) return ESP_ERR_INVALID_ARG;
    if (strlen(path) >= sizeof(s_request.path)) return ESP_ERR_INVALID_SIZE;
    radio_stream_format_t decoded;
    // WAV bypasses radio_decoder entirely, so it has no stream format of its
    // own and must be admitted separately.
    if (format != FILE_BROWSER_FORMAT_WAV && !stream_format_for(format, &decoded)) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    /* The volume can go while a play is on its way in - a drive pulled, a card
     * released when its source is left. Both mark themselves unmounted before
     * the mount is torn down, so this is what keeps a track from being opened
     * on a filesystem that is already going. */
    if (!file_storage_path_mounted(path)) return ESP_ERR_NOT_FOUND;

    xSemaphoreTake(s_control_lock, portMAX_DELAY);
    if (!wait_for_track_end()) {
        // Starting now would hand a second task the same request and the same
        // I2S channel; the caller logs and leaves the old track playing.
        xSemaphoreGive(s_control_lock);
        return ESP_ERR_TIMEOUT;
    }

    snprintf(s_request.path, sizeof(s_request.path), "%s", path);
    s_request.format = format;
    /* The previous task is gone; the status getter still reads the sheet, so
     * it is marked unused before it is overwritten. */
    atomic_store_explicit(&s_cue_active, false, memory_order_release);
    atomic_store_explicit(&s_cue_track, -1, memory_order_release);
    if (cue != NULL) {
        *s_cue = *cue;
        atomic_store_explicit(&s_cue_active, true, memory_order_release);
    }
    taskENTER_CRITICAL(&s_status_lock);
    memset(&s_status, 0, sizeof(s_status));
    // Cleared here rather than when the previous track ended, so nothing ever
    // reads the last track's performer against this one's file name.
    audio_tags_clear(&s_tags);
    /* The clearing counts as a change too: a track that turns out to have no
     * tags of its own has to move the screen off the previous track's rather
     * than leave it standing. */
    atomic_fetch_add_explicit(&s_tags_revision, 1U, memory_order_release);
    s_status.state = FILE_PLAYER_STATE_STARTING;
    snprintf(s_status.track, sizeof(s_status.track), "%s",
             display_name != NULL ? display_name : path);
    taskEXIT_CRITICAL(&s_status_lock);

    // Claimed before the task exists, so the window where it has been created
    // but has not yet run is already covered.
    atomic_store_explicit(&s_task_running, true, memory_order_release);
    const BaseType_t created =
        xTaskCreatePinnedToCore(file_player_task, "usb_play", FILE_PLAYER_TASK_STACK, NULL,
                                FILE_PLAYER_TASK_PRIORITY, NULL, FILE_PLAYER_TASK_CORE);
    if (created != pdPASS) {
        /* Not a shortage of memory but of *contiguous* memory: the stack has to
         * be one block, and internal SRAM here fragments as the radio, cover
         * art and mounts come and go. The region breakdown says which heap ran
         * out, which the free/largest pair alone does not. */
        ESP_LOGE(TAG, "no room for the playback task: internal_free=%u largest=%u",
                 (unsigned int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                                MALLOC_CAP_8BIT));
        heap_caps_print_heap_info(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        atomic_store_explicit(&s_task_running, false, memory_order_release);
        status_set_state(FILE_PLAYER_STATE_ERROR);
        xSemaphoreGive(s_control_lock);
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreGive(s_control_lock);
    return ESP_OK;
}

esp_err_t file_player_play(const char *path, const char *display_name,
                          file_browser_format_t format)
{
    return start_track(path, display_name, format, NULL);
}

esp_err_t file_player_play_cue(const char *path, const char *display_name,
                               file_browser_format_t format, const file_player_cue_t *cue)
{
    if (cue == NULL || cue->count == 0U || cue->count > FILE_PLAYER_CUE_TRACKS_MAX ||
        cue->first >= cue->count) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_cue == NULL) return ESP_ERR_NO_MEM;
    return start_track(path, display_name, format, cue);
}

esp_err_t file_player_stop(void)
{
    if (s_control_lock == NULL) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_control_lock, portMAX_DELAY);
    const bool stopped = wait_for_track_end();
    // Only claim STOPPED when it is: a caller told the track had stopped while
    // it is still writing audio has no way to notice, and acts on the lie.
    if (stopped) status_set_state(FILE_PLAYER_STATE_STOPPED);
    xSemaphoreGive(s_control_lock);
    return stopped ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t file_player_pause(void)
{
    if (!atomic_load_explicit(&s_task_running, memory_order_acquire)) {
        return ESP_ERR_INVALID_STATE;
    }
    atomic_store_explicit(&s_paused, true, memory_order_release);
    // Silence the DAC rather than letting the DMA repeat its last buffer, the
    // same reason the radio disables output on pause.
    (void)board_audio_set_enabled(false);
    status_set_state(FILE_PLAYER_STATE_PAUSED);
    return ESP_OK;
}

esp_err_t file_player_seek(uint32_t seconds)
{
    if (!atomic_load_explicit(&s_task_running, memory_order_acquire)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (seconds >= FILE_PLAYER_SEEK_NONE / 75U) return ESP_ERR_INVALID_ARG;
    uint32_t frames = seconds * 75U;
    // Within the .cue track on the air, like the time the bar shows.
    const int track = atomic_load_explicit(&s_cue_track, memory_order_acquire);
    if (atomic_load_explicit(&s_cue_active, memory_order_acquire) && track >= 0 &&
        (size_t)track < s_cue->count) {
        frames += s_cue->start_frames[track];
    }
    atomic_store_explicit(&s_seek_request, frames, memory_order_release);
    return ESP_OK;
}

esp_err_t file_player_seek_cue_track(size_t track)
{
    if (!atomic_load_explicit(&s_task_running, memory_order_acquire)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!atomic_load_explicit(&s_cue_active, memory_order_acquire) || track >= s_cue->count) {
        return ESP_ERR_INVALID_ARG;
    }
    atomic_store_explicit(&s_seek_request, s_cue->start_frames[track], memory_order_release);
    return ESP_OK;
}

esp_err_t file_player_resume(void)
{
    if (!atomic_load_explicit(&s_task_running, memory_order_acquire)) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t result = board_audio_set_enabled(true);
    if (result != ESP_OK) return result;
    atomic_store_explicit(&s_paused, false, memory_order_release);
    status_set_state(FILE_PLAYER_STATE_PLAYING);
    return ESP_OK;
}

void file_player_get_tags(audio_tags_t *tags)
{
    if (tags == NULL) return;
    taskENTER_CRITICAL(&s_status_lock);
    *tags = s_tags;
    taskEXIT_CRITICAL(&s_status_lock);
}

void file_player_get_status(file_player_status_t *status)
{
    if (status == NULL) return;
    taskENTER_CRITICAL(&s_status_lock);
    *status = s_status;
    taskEXIT_CRITICAL(&s_status_lock);
    status->buffer_percent =
        (uint8_t)atomic_load_explicit(&s_input_fill_percent, memory_order_relaxed);
    status->elapsed_seconds = atomic_load_explicit(&s_elapsed_seconds, memory_order_relaxed);
    status->total_seconds = atomic_load_explicit(&s_total_seconds, memory_order_relaxed);
    status->tags_revision = atomic_load_explicit(&s_tags_revision, memory_order_acquire);
    status->cue_track = -1;
    const int track = atomic_load_explicit(&s_cue_track, memory_order_acquire);
    if (!atomic_load_explicit(&s_cue_active, memory_order_acquire) || track < 0 ||
        (size_t)track >= s_cue->count) {
        return;
    }
    /* The track's own time and length, so the bar runs from its start to its
     * end rather than across the whole side. The last one runs to the end of
     * the file, whose length is only as good as the file's own. */
    status->cue_track = (int16_t)track;
    const uint32_t start = s_cue->start_frames[track];
    const uint32_t position = atomic_load_explicit(&s_position_frames, memory_order_relaxed);
    status->elapsed_seconds = position > start ? (position - start) / 75U : 0U;
    if ((size_t)track + 1U < s_cue->count) {
        status->total_seconds = (s_cue->start_frames[track + 1] - start) / 75U;
    } else {
        const uint64_t file_frames = (uint64_t)status->total_seconds * 75U;
        status->total_seconds = file_frames > start ? (uint32_t)((file_frames - start) / 75U) : 0U;
    }
}
