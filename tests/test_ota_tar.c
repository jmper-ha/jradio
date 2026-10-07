#include "ota_tar.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Archives built here the way tools/pack_www.py (Python's tarfile, ustar)
 * writes them, so the reader is tested against the format and not against
 * itself. */
typedef struct {
    uint8_t bytes[64 * 1024];
    size_t length;
} archive_t;

static void add_entry(archive_t *archive, const char *name, const char *body, char type)
{
    uint8_t *header = archive->bytes + archive->length;
    memset(header, 0, OTA_TAR_BLOCK);
    strncpy((char *)header, name, 100);
    const size_t size = strlen(body);
    snprintf((char *)header + 100, 8, "%07o", 0644);
    snprintf((char *)header + 108, 8, "%07o", 0);
    snprintf((char *)header + 116, 8, "%07o", 0);
    snprintf((char *)header + 124, 12, "%011o", (unsigned)size);
    snprintf((char *)header + 136, 12, "%011o", 0);
    header[156] = (uint8_t)type;
    memcpy(header + 257, "ustar\0" "00", 8);
    memset(header + 148, ' ', 8);
    unsigned sum = 0;
    for (size_t index = 0; index < OTA_TAR_BLOCK; ++index) sum += header[index];
    snprintf((char *)header + 148, 8, "%06o", sum);
    archive->length += OTA_TAR_BLOCK;
    memcpy(archive->bytes + archive->length, body, size);
    archive->length += (size + OTA_TAR_BLOCK - 1) / OTA_TAR_BLOCK * OTA_TAR_BLOCK;
}

static void add_end(archive_t *archive)
{
    /* Two end blocks, and the padding tarfile adds up to its 10 KB record. */
    archive->length += 2 * OTA_TAR_BLOCK;
    archive->length = (archive->length + 10239) / 10240 * 10240;
}

static archive_t *new_archive(void)
{
    return calloc(1, sizeof(archive_t));
}

/* A sink that keeps what it was given, the way the device would write it. */
typedef struct {
    char names[8][64];
    char bodies[8][256];
    size_t count;
    size_t fail_at;  /* 1-based entry whose data fails, 0 for never */
    int open;
} collected_t;

static bool sink_begin(void *context, ota_tar_entry_t kind, const char *name, size_t size)
{
    collected_t *seen = context;
    (void)size;
    assert(!seen->open);
    assert(seen->count < 8);
    snprintf(seen->names[seen->count], 64, "%s%s", kind == OTA_TAR_ENTRY_VERSION ? "=" : "", name);
    seen->bodies[seen->count][0] = '\0';
    seen->open = 1;
    return true;
}

static bool sink_data(void *context, const uint8_t *bytes, size_t length)
{
    collected_t *seen = context;
    assert(seen->open);
    if (seen->fail_at == seen->count + 1) return false;
    strncat(seen->bodies[seen->count], (const char *)bytes, length);
    return true;
}

static bool sink_end(void *context)
{
    collected_t *seen = context;
    assert(seen->open);
    seen->open = 0;
    ++seen->count;
    return true;
}

static ota_tar_result_t read_all(const archive_t *archive, size_t piece, collected_t *seen)
{
    const ota_tar_sink_t sink = {sink_begin, sink_data, sink_end, seen};
    ota_tar_t tar;
    ota_tar_init(&tar);
    for (size_t at = 0; at < archive->length; at += piece) {
        const size_t take = archive->length - at < piece ? archive->length - at : piece;
        if (ota_tar_feed(&tar, archive->bytes + at, take, &sink) != OTA_TAR_OK) break;
    }
    return ota_tar_finish(&tar);
}

static void test_the_files_come_out_whole_in_any_pieces(void)
{
    archive_t *archive = new_archive();
    add_entry(archive, "version.json", "{\"version\":\"v1.6.0\"}", '0');
    add_entry(archive, "www/index.html", "<!doctype html>", '0');
    add_entry(archive, "www/app.js.gz", "", '0');
    add_entry(archive, "www/style.css", "body{}", '0');
    add_end(archive);
    /* One byte at a time cuts every header and every file; 4096 is what the
     * device reads; 511 never lines up with a block. */
    const size_t pieces[] = {1, 511, 4096, 1 << 20};
    for (size_t index = 0; index < sizeof(pieces) / sizeof(pieces[0]); ++index) {
        collected_t seen = {0};
        assert(read_all(archive, pieces[index], &seen) == OTA_TAR_OK);
        assert(seen.count == 4);
        assert(strcmp(seen.names[0], "=version.json") == 0);
        assert(strcmp(seen.bodies[0], "{\"version\":\"v1.6.0\"}") == 0);
        assert(strcmp(seen.names[1], "index.html") == 0);
        assert(strcmp(seen.bodies[1], "<!doctype html>") == 0);
        assert(strcmp(seen.names[2], "app.js.gz") == 0);
        assert(strcmp(seen.bodies[2], "") == 0);
        assert(strcmp(seen.names[3], "style.css") == 0);
    }
    free(archive);
}

static void test_names_that_leave_the_directory_refuse_the_archive(void)
{
    const char *hostile[] = {"www/../config/wifi.json", "www/a/b.js", "config/wifi.json",
                             "/www/app.js", "www/.hidden", "www/", "www/a b.js", "app.js"};
    for (size_t index = 0; index < sizeof(hostile) / sizeof(hostile[0]); ++index) {
        archive_t *archive = new_archive();
        add_entry(archive, "version.json", "{}", '0');
        add_entry(archive, hostile[index], "x", '0');
        add_end(archive);
        collected_t seen = {0};
        assert(read_all(archive, 4096, &seen) == OTA_TAR_BAD_NAME);
        assert(seen.count == 1);
        free(archive);
    }
}

static void test_links_and_directories_are_not_files(void)
{
    const char types[] = {'1', '2', '3', '5'};
    for (size_t index = 0; index < sizeof(types); ++index) {
        archive_t *archive = new_archive();
        add_entry(archive, "version.json", "{}", '0');
        add_entry(archive, "www/app.js", "", types[index]);
        add_end(archive);
        collected_t seen = {0};
        assert(read_all(archive, 4096, &seen) == OTA_TAR_BAD_NAME);
        free(archive);
    }
}

static void test_the_stamp_comes_first_and_once(void)
{
    archive_t *archive = new_archive();
    add_entry(archive, "www/app.js", "x", '0');
    add_entry(archive, "version.json", "{}", '0');
    add_end(archive);
    collected_t seen = {0};
    assert(read_all(archive, 4096, &seen) == OTA_TAR_NO_VERSION);
    assert(seen.count == 0);
    free(archive);

    archive = new_archive();
    add_entry(archive, "version.json", "{}", '0');
    add_entry(archive, "version.json", "{}", '0');
    add_end(archive);
    memset(&seen, 0, sizeof(seen));
    assert(read_all(archive, 4096, &seen) == OTA_TAR_NO_VERSION);
    free(archive);

    /* An archive with nothing in it installs nothing. */
    archive = new_archive();
    add_end(archive);
    memset(&seen, 0, sizeof(seen));
    assert(read_all(archive, 4096, &seen) == OTA_TAR_NO_VERSION);
    free(archive);
}

static void test_a_cut_or_garbled_archive_is_not_installed(void)
{
    archive_t *archive = new_archive();
    add_entry(archive, "version.json", "{}", '0');
    add_entry(archive, "www/app.js", "console.log(1)", '0');
    const size_t whole = archive->length;
    add_end(archive);

    /* Cut inside the last file, and cut before the end blocks. */
    archive_t cut = *archive;
    cut.length = whole - OTA_TAR_BLOCK + 5;
    collected_t seen = {0};
    assert(read_all(&cut, 4096, &seen) == OTA_TAR_TRUNCATED);
    cut.length = whole;
    memset(&seen, 0, sizeof(seen));
    assert(read_all(&cut, 4096, &seen) == OTA_TAR_TRUNCATED);

    /* One flipped byte in a header is a checksum that does not add up. */
    archive->bytes[2 * OTA_TAR_BLOCK + 3] ^= 1;
    memset(&seen, 0, sizeof(seen));
    assert(read_all(archive, 4096, &seen) == OTA_TAR_BAD_HEADER);
    free(archive);

    /* Not a tar at all: a firmware sent to the wrong place. */
    archive = new_archive();
    archive->bytes[0] = 0xE9;
    archive->length = 4096;
    memset(&seen, 0, sizeof(seen));
    assert(read_all(archive, 4096, &seen) == OTA_TAR_BAD_HEADER);
    free(archive);
}

static void test_a_file_too_large_is_refused_before_it_is_written(void)
{
    archive_t *archive = new_archive();
    add_entry(archive, "version.json", "{}", '0');
    add_entry(archive, "www/big.js", "", '0');
    /* Rewrite the size field to just over the limit, and the checksum. */
    uint8_t *header = archive->bytes + 2 * OTA_TAR_BLOCK;  /* after the stamp and its block */
    snprintf((char *)header + 124, 12, "%011o", (unsigned)OTA_TAR_FILE_MAX + 1U);
    memset(header + 148, ' ', 8);
    unsigned sum = 0;
    for (size_t index = 0; index < OTA_TAR_BLOCK; ++index) sum += header[index];
    snprintf((char *)header + 148, 8, "%06o", sum);
    add_end(archive);
    collected_t seen = {0};
    assert(read_all(archive, 4096, &seen) == OTA_TAR_TOO_BIG);
    assert(seen.count == 1);
    free(archive);
}

static void test_a_failed_write_stops_the_archive(void)
{
    archive_t *archive = new_archive();
    add_entry(archive, "version.json", "{}", '0');
    add_entry(archive, "www/app.js", "x", '0');
    add_entry(archive, "www/b.js", "y", '0');
    add_end(archive);
    collected_t seen = {.fail_at = 2};
    assert(read_all(archive, 4096, &seen) == OTA_TAR_SINK);
    assert(strcmp(ota_tar_result_code(OTA_TAR_SINK), "write") == 0);
    free(archive);
}

int main(void)
{
    test_the_files_come_out_whole_in_any_pieces();
    test_names_that_leave_the_directory_refuse_the_archive();
    test_links_and_directories_are_not_files();
    test_the_stamp_comes_first_and_once();
    test_a_cut_or_garbled_archive_is_not_installed();
    test_a_file_too_large_is_refused_before_it_is_written();
    test_a_failed_write_stops_the_archive();
    puts("ota tar tests passed");
    return 0;
}
