#include <stdarg.h>
#include <string.h>

#include "yandex_md5.h"

/* RFC 1321, section 3.4: T[i] = floor(2^32 * abs(sin(i))), i in radians. */
static const uint32_t MD5_T[64] = {
    0xd76aa478U, 0xe8c7b756U, 0x242070dbU, 0xc1bdceeeU, 0xf57c0fafU, 0x4787c62aU,
    0xa8304613U, 0xfd469501U, 0x698098d8U, 0x8b44f7afU, 0xffff5bb1U, 0x895cd7beU,
    0x6b901122U, 0xfd987193U, 0xa679438eU, 0x49b40821U, 0xf61e2562U, 0xc040b340U,
    0x265e5a51U, 0xe9b6c7aaU, 0xd62f105dU, 0x02441453U, 0xd8a1e681U, 0xe7d3fbc8U,
    0x21e1cde6U, 0xc33707d6U, 0xf4d50d87U, 0x455a14edU, 0xa9e3e905U, 0xfcefa3f8U,
    0x676f02d9U, 0x8d2a4c8aU, 0xfffa3942U, 0x8771f681U, 0x6d9d6122U, 0xfde5380cU,
    0xa4beea44U, 0x4bdecfa9U, 0xf6bb4b60U, 0xbebfbc70U, 0x289b7ec6U, 0xeaa127faU,
    0xd4ef3085U, 0x04881d05U, 0xd9d4d039U, 0xe6db99e5U, 0x1fa27cf8U, 0xc4ac5665U,
    0xf4292244U, 0x432aff97U, 0xab9423a7U, 0xfc93a039U, 0x655b59c3U, 0x8f0ccc92U,
    0xffeff47dU, 0x85845dd1U, 0x6fa87e4fU, 0xfe2ce6e0U, 0xa3014314U, 0x4e0811a1U,
    0xf7537e82U, 0xbd3af235U, 0x2ad7d2bbU, 0xeb86d391U,
};

static const uint8_t MD5_SHIFT[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
};

static uint32_t md5_rotate(uint32_t value, uint8_t bits)
{
    return (value << bits) | (value >> (32U - bits));
}

static void md5_transform(uint32_t state[4], const uint8_t block[64])
{
    uint32_t words[16];
    for (size_t index = 0U; index < 16U; ++index) {
        words[index] = (uint32_t)block[index * 4U] |
                       ((uint32_t)block[index * 4U + 1U] << 8U) |
                       ((uint32_t)block[index * 4U + 2U] << 16U) |
                       ((uint32_t)block[index * 4U + 3U] << 24U);
    }

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];

    for (uint32_t step = 0U; step < 64U; ++step) {
        uint32_t mixed;
        uint32_t word;
        if (step < 16U) {
            mixed = (b & c) | (~b & d);
            word = step;
        } else if (step < 32U) {
            mixed = (d & b) | (~d & c);
            word = (5U * step + 1U) % 16U;
        } else if (step < 48U) {
            mixed = b ^ c ^ d;
            word = (3U * step + 5U) % 16U;
        } else {
            mixed = c ^ (b | ~d);
            word = (7U * step) % 16U;
        }
        const uint32_t rotated = a + mixed + MD5_T[step] + words[word];
        a = d;
        d = c;
        c = b;
        b = b + md5_rotate(rotated, MD5_SHIFT[step]);
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

void yandex_md5_init(yandex_md5_t *context)
{
    context->state[0] = 0x67452301U;
    context->state[1] = 0xefcdab89U;
    context->state[2] = 0x98badcfeU;
    context->state[3] = 0x10325476U;
    context->bits = 0U;
    context->pending = 0U;
    memset(context->block, 0, sizeof(context->block));
}

void yandex_md5_update(yandex_md5_t *context, const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    context->bits += (uint64_t)length * 8U;
    while (length > 0U) {
        const size_t room = sizeof(context->block) - context->pending;
        const size_t take = (length < room) ? length : room;
        memcpy(context->block + context->pending, bytes, take);
        context->pending += take;
        bytes += take;
        length -= take;
        if (context->pending == sizeof(context->block)) {
            md5_transform(context->state, context->block);
            context->pending = 0U;
        }
    }
}

void yandex_md5_final(yandex_md5_t *context, uint8_t digest[YANDEX_MD5_DIGEST_SIZE])
{
    const uint64_t bits = context->bits;
    static const uint8_t padding = 0x80U;
    static const uint8_t zero = 0x00U;

    yandex_md5_update(context, &padding, 1U);
    while (context->pending != 56U) {
        yandex_md5_update(context, &zero, 1U);
    }
    /* The length goes in little-endian, and must not be counted into itself. */
    uint8_t length[8];
    for (size_t index = 0U; index < 8U; ++index) {
        length[index] = (uint8_t)((bits >> (8U * index)) & 0xFFU);
    }
    memcpy(context->block + 56U, length, sizeof(length));
    md5_transform(context->state, context->block);

    for (size_t word = 0U; word < 4U; ++word) {
        for (size_t byte = 0U; byte < 4U; ++byte) {
            digest[word * 4U + byte] = (uint8_t)((context->state[word] >> (8U * byte)) & 0xFFU);
        }
    }
}

void yandex_md5_hex_of(char output[YANDEX_MD5_HEX_SIZE], const char *first, ...)
{
    yandex_md5_t context;
    yandex_md5_init(&context);

    va_list arguments;
    va_start(arguments, first);
    for (const char *part = first; part != NULL; part = va_arg(arguments, const char *)) {
        yandex_md5_update(&context, part, strlen(part));
    }
    va_end(arguments);

    uint8_t digest[YANDEX_MD5_DIGEST_SIZE];
    yandex_md5_final(&context, digest);
    static const char HEX[] = "0123456789abcdef";
    for (size_t index = 0U; index < YANDEX_MD5_DIGEST_SIZE; ++index) {
        output[index * 2U] = HEX[digest[index] >> 4U];
        output[index * 2U + 1U] = HEX[digest[index] & 0x0FU];
    }
    output[YANDEX_MD5_HEX_SIZE - 1U] = '\0';
}
