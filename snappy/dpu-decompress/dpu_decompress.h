/**
 * Decompression on DPU.
 */

#ifndef _DPU_DECOMPRESS_H_
#define _DPU_DECOMPRESS_H_

#include "common.h"
#include "dpu_common_decompress.h"
#include <seqread.h> // sequential reader

#define GET_ELEMENT_TYPE(_tag)  (_tag & BITMASK(2))
#define GET_LENGTH_1_BYTE(_tag) ((_tag >> 2) & BITMASK(3))
#define GET_OFFSET_1_BYTE(_tag) ((_tag >> 5) & BITMASK(3))
#define GET_LENGTH_2_BYTE(_tag) ((_tag >> 2) & BITMASK(6))

// Length of the "append window" and "read window" in the
// out_buffer_context
#define OUT_BUFFER_LENGTH 256

// Sequential reader cache size must be the same as the
// append window size, since we memcpy from one to the other
#undef SEQREAD_CACHE_SIZE
#define SEQREAD_CACHE_SIZE OUT_BUFFER_LENGTH

// Return values
typedef enum {
    SNAPPY_OK = 0,              // Success code
    SNAPPY_INVALID_INPUT,       // Input file has an invalid format
    SNAPPY_BUFFER_TOO_SMALL     // Input or output file size is too large
} snappy_status;

// Snappy tag types
enum element_type
{
    EL_TYPE_LITERAL,
    EL_TYPE_COPY_1,
    EL_TYPE_COPY_2,
    EL_TYPE_COPY_4
};


/**
 * Perform the Snappy decompression on the DPU.
 *
 * @param input: holds input buffer information
 * @param output: holds output buffer information
 * @return SNAPPY_OK if successful, error code otherwise
 */
snappy_status dpu_uncompress(struct in_buffer_context *input, struct out_buffer_context *output);

#endif

