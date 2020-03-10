/**
 * Decompression on DPU.
 */

#ifndef _DPU_DECOMPRESS_H_
#define _DPU_DECOMPRESS_H_

#include <stddef.h>

/**
 * Uncompress a snappy compressed buffer.
 * @param compressed Input buffer containing the compressed data.
 * @param length Length of the compressed buffer.
 * @param uncompressed Output buffer. This buffer should be at least as long
 *     as dpu_uncompressed_length(compressed).
 * @return 0 if successful.
 */
int dpu_uncompress(const char *compressed, size_t length, char *uncompressed);

/**
 * Return the uncompressed length of the compressed file. If size can't be
 * parsed, return -1.
 * @param compressed Input buffer with the compressed data.
 * @param length Length of the compressed buffer.
 * @return 0 if successful.
 */
int dpu_uncompressed_length(const char *compressed, size_t length,
                            size_t *result);

#endif

