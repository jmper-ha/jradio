#include "ota_www.h"

#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Pages the device is no use without: an archive missing either would leave
 * a radio whose web interface cannot even offer the next update. */
static const char *const k_required[] = {"index.html", "settings.html", "update.js"};

static void join(char *out, size_t size, const char *root, const char *a, const char *b)
{
    snprintf(out, size, "%s/%s%s%s", root, a, b != NULL ? "/" : "", b != NULL ? b : "");
}

static bool exists(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0;
}

/* The web directory is flat, and so is everything this removes. */
static void remove_flat_directory(const char *path)
{
    DIR *dir = opendir(path);
    if (dir == NULL) return;
    struct dirent *entry;
    char file[OTA_WWW_PATH_MAX + 64];
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        const int length = snprintf(file, sizeof(file), "%s/%s", path, entry->d_name);
        /* Nothing this writes has a name that long; anything else is left. */
        if (length < 0 || (size_t)length >= sizeof(file)) continue;
        (void)unlink(file);
    }
    closedir(dir);
    (void)rmdir(path);
}

void ota_www_discard(const char *root)
{
    char path[OTA_WWW_PATH_MAX];
    join(path, sizeof(path), root, "www.new", NULL);
    remove_flat_directory(path);
}

bool ota_www_stamp_version(const char *stamp, size_t length, char *out, size_t out_size)
{
    static const char key[] = "\"version\":\"";
    if (out_size == 0U) return false;
    out[0] = '\0';
    for (size_t at = 0U; at + sizeof(key) - 1U <= length; ++at) {
        if (memcmp(stamp + at, key, sizeof(key) - 1U) != 0) continue;
        size_t from = at + sizeof(key) - 1U;
        size_t used = 0U;
        while (from < length && stamp[from] != '"') {
            /* git describe output: nothing in it needs escaping, and a stamp
             * that has an escape in it is not one stamp_version.py wrote. */
            if (stamp[from] == '\\' || used + 1U >= out_size) return false;
            out[used++] = stamp[from++];
        }
        if (from >= length || used == 0U) return false;
        out[used] = '\0';
        return true;
    }
    return false;
}

/* The tar sink: one file at a time, straight to the file system. */
static bool sink_begin(void *context, ota_tar_entry_t kind, const char *name, size_t size)
{
    ota_www_t *www = context;
    (void)kind;
    (void)size;
    join(www->path, sizeof(www->path), www->root, "www.new", name);
    www->file = fopen(www->path, "wb");
    return www->file != NULL;
}

static bool sink_data(void *context, const uint8_t *bytes, size_t length)
{
    ota_www_t *www = context;
    return fwrite(bytes, 1U, length, www->file) == length;
}

static bool sink_end(void *context)
{
    ota_www_t *www = context;
    const bool closed = fclose(www->file) == 0;
    www->file = NULL;
    return closed;
}

bool ota_www_begin(ota_www_t *www, const char *root)
{
    memset(www, 0, sizeof(*www));
    snprintf(www->root, sizeof(www->root), "%s", root);
    ota_tar_init(&www->tar);
    ota_www_discard(root);
    char path[OTA_WWW_PATH_MAX];
    join(path, sizeof(path), root, "www.new", NULL);
    return mkdir(path, 0755) == 0;
}

ota_tar_result_t ota_www_write(ota_www_t *www, const void *bytes, size_t length)
{
    const ota_tar_sink_t sink = {sink_begin, sink_data, sink_end, www};
    return ota_tar_feed(&www->tar, bytes, length, &sink);
}

ota_tar_result_t ota_www_finish(ota_www_t *www)
{
    ota_tar_result_t result = ota_tar_finish(&www->tar);
    char path[OTA_WWW_PATH_MAX];
    for (size_t index = 0U; result == OTA_TAR_OK && index < sizeof(k_required) / sizeof(k_required[0]);
         ++index) {
        join(path, sizeof(path), www->root, "www.new", k_required[index]);
        if (!exists(path)) result = OTA_TAR_NO_VERSION;
    }
    if (result == OTA_TAR_OK) {
        join(path, sizeof(path), www->root, "www.new", "version.json");
        char stamp[256];
        size_t length = 0U;
        FILE *file = fopen(path, "rb");
        if (file != NULL) {
            length = fread(stamp, 1U, sizeof(stamp), file);
            fclose(file);
        }
        if (!ota_www_stamp_version(stamp, length, www->version, sizeof(www->version))) {
            result = OTA_TAR_NO_VERSION;
        }
    }
    if (result != OTA_TAR_OK) ota_www_abort(www);
    return result;
}

void ota_www_abort(ota_www_t *www)
{
    if (www->file != NULL) {
        fclose(www->file);
        www->file = NULL;
    }
    ota_www_discard(www->root);
}

/* The stamp out of the new pages and into config/, where the About card
 * reads it - last, so a cut before it is finished by the boot cleanup. */
static bool move_stamp(const char *root)
{
    char from[OTA_WWW_PATH_MAX];
    char to[OTA_WWW_PATH_MAX];
    join(from, sizeof(from), root, "www", "version.json");
    if (!exists(from)) return true;
    join(to, sizeof(to), root, "config", "version.json");
    (void)unlink(to);
    return rename(from, to) == 0;
}

bool ota_www_apply(const char *root)
{
    char live[OTA_WWW_PATH_MAX];
    char staged[OTA_WWW_PATH_MAX];
    char old[OTA_WWW_PATH_MAX];
    join(live, sizeof(live), root, "www", NULL);
    join(staged, sizeof(staged), root, "www.new", NULL);
    join(old, sizeof(old), root, "www.old", NULL);
    if (!exists(staged)) return false;
    remove_flat_directory(old);
    if (exists(live) && rename(live, old) != 0) return false;
    if (rename(staged, live) != 0) {
        /* Put the old pages back rather than leave none. */
        (void)rename(old, live);
        return false;
    }
    const bool stamped = move_stamp(root);
    remove_flat_directory(old);
    return stamped;
}

void ota_www_boot_cleanup(const char *root)
{
    char live[OTA_WWW_PATH_MAX];
    char old[OTA_WWW_PATH_MAX];
    join(live, sizeof(live), root, "www", NULL);
    join(old, sizeof(old), root, "www.old", NULL);
    /* Cut between the two renames: the old pages are the only ones whole. */
    if (!exists(live) && exists(old)) (void)rename(old, live);
    (void)move_stamp(root);
    remove_flat_directory(old);
    /* An archive uploaded and never confirmed - the restart came first. */
    ota_www_discard(root);
}
