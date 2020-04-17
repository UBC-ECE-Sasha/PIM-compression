/**
 * Decompression on DPU.
 */

#ifndef _DPU_DECOMPRESS_H_
#define _DPU_DECOMPRESS_H_

//#include <stddef.h>
#include "../dpu_snappy.h"

/**
 * Uncompress a snappy compressed buffer. If successful, return 0 and write
 * the size of the uncompressed contents to uncompressed_length.
 * @param compressed Input buffer containing the compressed data.
 * @param length Length of the compressed buffer.
 * @param uncompressed Output buffer. This buffer should be at least as long
 *     as dpu_uncompressed_length(compressed).
 * @return 0 if successful.
 */
snappy_status dpu_uncompress(struct buffer_context *input, struct buffer_context *output);

#endif

