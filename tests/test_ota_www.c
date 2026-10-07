#define _DEFAULT_SOURCE
#include "ota_www.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The web half of an update in a temporary directory standing for /littlefs:
 * the archive unpacked beside the live pages, the swap, and the boot that
 * finds a swap cut short. */

static char g_root[64];

static void path_of(char *out, size_t size, const char *relative)
{
    snprintf(out, size, "%s/%s", g_root, relative);
}

static void write_file(const char *relative, const char *body)
{
    char path[160];
    path_of(path, sizeof(path), relative);
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    fputs(body, file);
    fclose(file);
}

static int read_file(const char *relative, char *out, size_t size)
{
    char path[160];
    path_of(path, sizeof(path), relative);
    FILE *file = fopen(path, "rb");
    if (file == NULL) return 0;
    const size_t length = fread(out, 1, size - 1, file);
    out[length] = '\0';
    fclose(file);
    return 1;
}

static int present(const char *relative)
{
    char path[160];
    struct stat info;
    path_of(path, sizeof(path), relative);
    return stat(path, &info) == 0;
}

static void make_dir(const char *relative)
{
    char path[160];
    path_of(path, sizeof(path), relative);
    assert(mkdir(path, 0755) == 0);
}

/* A device as it is before the update: old pages, its own settings. */
static void fresh_device(void)
{
    snprintf(g_root, sizeof(g_root), "/tmp/jradio-ota-www-XXXXXX");
    assert(mkdtemp(g_root) != NULL);
    make_dir("www");
    make_dir("config");
    write_file("www/index.html", "old index");
    write_file("www/settings.html", "old settings");
    write_file("www/update.js", "old update");
    write_file("www/gone.js", "a page the new version no longer has");
    write_file("config/version.json", "{\"version\":\"v1.5.5\",\"built\":\"2026-10-06\"}");
    write_file("config/wifi.json", "{\"networks\":[]}");
}

static void remove_device(void)
{
    char command[128];
    snprintf(command, sizeof(command), "rm -rf %s", g_root);
    assert(system(command) == 0);
}

typedef struct {
    uint8_t bytes[32 * 1024];
    size_t length;
} archive_t;

static void add_entry(archive_t *archive, const char *name, const char *body)
{
    uint8_t *header = archive->bytes + archive->length;
    memset(header, 0, OTA_TAR_BLOCK);
    strncpy((char *)header, name, 100);
    const size_t size = strlen(body);
    snprintf((char *)header + 124, 12, "%011o", (unsigned)size);
    header[156] = '0';
    memcpy(header + 257, "ustar\0" "00", 8);
    memset(header + 148, ' ', 8);
    unsigned sum = 0;
    for (size_t index = 0; index < OTA_TAR_BLOCK; ++index) sum += header[index];
    snprintf((char *)header + 148, 8, "%06o", sum);
    archive->length += OTA_TAR_BLOCK;
    memcpy(archive->bytes + archive->length, body, size);
    archive->length += (size + OTA_TAR_BLOCK - 1) / OTA_TAR_BLOCK * OTA_TAR_BLOCK;
}

static archive_t *new_version_archive(int with_update_page)
{
    archive_t *archive = calloc(1, sizeof(*archive));
    add_entry(archive, "version.json", "{\"version\":\"v1.6.0\",\"built\":\"2026-10-20\"}");
    add_entry(archive, "www/index.html", "new index");
    add_entry(archive, "www/settings.html", "new settings");
    if (with_update_page) add_entry(archive, "www/update.js", "new update");
    archive->length += 2 * OTA_TAR_BLOCK;
    return archive;
}

static ota_tar_result_t upload(const archive_t *archive, ota_www_t *www)
{
    assert(ota_www_begin(www, g_root));
    for (size_t at = 0; at < archive->length; at += 700) {
        const size_t take = archive->length - at < 700 ? archive->length - at : 700;
        if (ota_www_write(www, archive->bytes + at, take) != OTA_TAR_OK) break;
    }
    return ota_www_finish(www);
}

static void test_an_archive_waits_beside_the_live_pages_until_applied(void)
{
    fresh_device();
    archive_t *archive = new_version_archive(1);
    static ota_www_t www;
    assert(upload(archive, &www) == OTA_TAR_OK);
    assert(strcmp(www.version, "v1.6.0") == 0);
    /* Nothing live has changed yet: a declined update leaves the old pages. */
    char body[128];
    assert(read_file("www/index.html", body, sizeof(body)) && strcmp(body, "old index") == 0);
    assert(present("www.new/index.html"));

    assert(ota_www_apply(g_root));
    assert(read_file("www/index.html", body, sizeof(body)) && strcmp(body, "new index") == 0);
    /* A page the new version dropped is gone, not left over to be served. */
    assert(!present("www/gone.js"));
    /* The stamp moved to where the About card reads it, out of the pages. */
    assert(!present("www/version.json"));
    assert(read_file("config/version.json", body, sizeof(body)) && strstr(body, "v1.6.0"));
    /* And what the archive never names is untouched. */
    assert(read_file("config/wifi.json", body, sizeof(body)) &&
           strcmp(body, "{\"networks\":[]}") == 0);
    assert(!present("www.new") && !present("www.old"));
    free(archive);
    remove_device();
}

static void test_an_archive_without_the_pages_a_device_needs_is_dropped(void)
{
    fresh_device();
    archive_t *archive = new_version_archive(0);
    static ota_www_t www;
    assert(upload(archive, &www) != OTA_TAR_OK);
    assert(!present("www.new"));
    assert(!ota_www_apply(g_root));
    char body[64];
    assert(read_file("www/index.html", body, sizeof(body)) && strcmp(body, "old index") == 0);
    free(archive);
    remove_device();
}

static void test_a_declined_update_takes_its_pages_away(void)
{
    fresh_device();
    archive_t *archive = new_version_archive(1);
    static ota_www_t www;
    assert(upload(archive, &www) == OTA_TAR_OK);
    ota_www_discard(g_root);
    assert(!present("www.new"));
    assert(present("www/gone.js"));
    free(archive);
    remove_device();
}

static void test_a_boot_finishes_or_undoes_a_swap_cut_short(void)
{
    char body[128];
    /* Cut after the live pages were moved aside and before the new ones
     * took their place: the old ones come back. */
    fresh_device();
    char from[160], to[160];
    path_of(from, sizeof(from), "www");
    path_of(to, sizeof(to), "www.old");
    assert(rename(from, to) == 0);
    make_dir("www.new");
    write_file("www.new/index.html", "half of a new version");
    ota_www_boot_cleanup(g_root);
    assert(read_file("www/index.html", body, sizeof(body)) && strcmp(body, "old index") == 0);
    assert(!present("www.old") && !present("www.new"));
    remove_device();

    /* Cut after the swap and before the stamp moved: the stamp is moved. */
    fresh_device();
    write_file("www/version.json", "{\"version\":\"v1.6.0\"}");
    make_dir("www.old");
    write_file("www.old/index.html", "old index");
    ota_www_boot_cleanup(g_root);
    assert(!present("www/version.json"));
    assert(read_file("config/version.json", body, sizeof(body)) && strstr(body, "v1.6.0"));
    assert(!present("www.old"));
    remove_device();
}

static void test_the_stamp_version_is_read_as_the_build_writes_it(void)
{
    char version[32];
    const char stamp[] = "{\"version\":\"v1.5.5-4-ge5fcf3d\",\"built\":\"2026-10-07\"}";
    assert(ota_www_stamp_version(stamp, strlen(stamp), version, sizeof(version)));
    assert(strcmp(version, "v1.5.5-4-ge5fcf3d") == 0);
    assert(!ota_www_stamp_version("{}", 2, version, sizeof(version)));
    assert(!ota_www_stamp_version("{\"version\":\"v1", 15, version, sizeof(version)));
    assert(!ota_www_stamp_version("{\"version\":\"\"}", 14, version, sizeof(version)));
    char small[4];
    assert(!ota_www_stamp_version(stamp, strlen(stamp), small, sizeof(small)));
}

int main(void)
{
    test_an_archive_waits_beside_the_live_pages_until_applied();
    test_an_archive_without_the_pages_a_device_needs_is_dropped();
    test_a_declined_update_takes_its_pages_away();
    test_a_boot_finishes_or_undoes_a_swap_cut_short();
    test_the_stamp_version_is_read_as_the_build_writes_it();
    puts("ota www tests passed");
    return 0;
}
