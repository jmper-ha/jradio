#pragma once

#include <stddef.h>
#include <stdint.h>

/* MD5, needed for exactly one thing: the signature Yandex puts in a track's
 * download URL (md5(salt + path-without-leading-slash + s)).
 *
 * It is written out here rather than taken from mbedTLS because the host tests
 * build these files with plain gcc and no ESP-IDF: a signature that is only
 * exercised on the device is a signature nobody checks. RFC 1321's own test
 * vectors are in the test file.
 *
 * Not for anything security-bearing - MD5 has no business there, and this is
 * a checksum the server asked for, not a defence. */

#define YANDEX_MD5_DIGEST_SIZE 16U
/* 32 hex characters and the terminator. */
#define YANDEX_MD5_HEX_SIZE 33U

typedef struct {
    uint32_t state[4];
    uint64_t bits;
    uint8_t block[64];
    size_t pending;
} yandex_md5_t;

void yandex_md5_init(yandex_md5_t *context);
void yandex_md5_update(yandex_md5_t *context, const void *data, size_t length);
void yandex_md5_final(yandex_md5_t *context, uint8_t digest[YANDEX_MD5_DIGEST_SIZE]);

/* Convenience for the only caller: hex digest of a run of strings, ending at
 * the first NULL pointer. */
void yandex_md5_hex_of(char output[YANDEX_MD5_HEX_SIZE], const char *first, ...);
