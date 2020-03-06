#include <stdint.h>

#include "dpu_decompress.h"

/**
 * Attempts to parse a varint32 from a prefix of the bytes in [ptr,limit-1].
 * Never reads a character at or beyond limit. If a valid/terminated varint32
 * was found in the range, stores it in *OUTPUT and returns a pointer just
 * past the last byte of the varint32. Else returns NULL. On success,
 * "result <= limit".
 */
static const char *varint_parse32_with_limit(const char *p, const char *l,
                                             uint32_t *output) {
    const unsigned char *ptr = (const unsigned char *)(p);
    const unsigned char *limit = (const unsigned char *)(l);
    uint32_t b, result;

    if (ptr >= limit) return NULL;
    b = *(ptr++);
    result = b & 127;

    if (b < 128) goto done;
    if (ptr >= limit) return NULL;

    b = *(ptr++);
    result |= (b & 127) << 7;

    if (b < 128) goto done;
    if (ptr >= limit) return NULL;

    b = *(ptr++);
    result |= (b & 127) << 14;

    if (b < 128) goto done;
    if (ptr >= limit) return NULL;

    b = *(ptr++);
    result |= (b & 127) << 21;

    if (b < 128) goto done;
    if (ptr >= limit) return NULL;

    b = *(ptr++);
    result |= (b & 127) << 28;

    if (b < 16) goto done;

    return NULL;  /* Value is too long to be a varint32 */

done:
    *output = result;
    return (const char *)(ptr);
}

int dpu_uncompress(const char* compressed, 
                   size_t compressed_len,
                   char* uncompressed) {
    // TODO: implement this
    (void) compressed;
    (void) compressed_len;
    (void) uncompressed;
    return 0;
}

int dpu_uncompressed_length(const char* compressed, size_t compressed_len,
                            size_t *result) {
    uint32_t v;
    const char *limit = compressed + compressed_len;
    if (varint_parse32_with_limit(compressed, limit, &v) == NULL) {
        return -1;
    }
    *result = v;
    return 0;
}

