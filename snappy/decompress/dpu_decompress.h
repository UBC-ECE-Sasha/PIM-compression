/**
 * Decompression on DPU.
 */

#ifndef _DPU_DECOMPRESS_H_
#define _DPU_DECOMPRESS_H_

#include <stddef.h>

size_t dpu_uncompress(const char* compressed, 
                      size_t compressed_len,
                      char* uncompressed,
                      size_t uncompressed_len);

size_t dpu_uncompressed_length(const char* compressed, size_t compressed_len);

#endif

