/**
 * Compression on DPU.
 */

#ifndef _DPU_COMPRESS_H_
#define _DPU_COMPRESS_H_

#include "common.h"
#include "dpu_common_compress.h"
#include <seqread.h>

// Length of the "append window" in out_buffer_context
#define OUT_BUFFER_LENGTH 256

// Sequential reader cache size must be the same as the
// append window size, since we memcpy from one to the other
#undef SEQREAD_CACHE_SIZE
#define SEQREAD_CACHE_SIZE OUT_BUFFER_LENGTH

/* LZ4 constants */
#define MINMATCH 		 4
#define WILDCOPYLENGTH   8
#define MFLIMIT 		 12
#define LASTLITERALS 	 5
#define LZ4_DISTANCE_MAX 65535
#define MATCH_SAFEGUARD_DISTANCE  ((2*WILDCOPYLENGTH) - MINMATCH)   /* ensure it's possible to write 2 x wildcopyLength without overflowing output buffer */

/* Encoding Constants*/
#define ML_BITS 4
#define ML_MASK ((1U<<ML_BITS)-1)
#define RUN_BITS (8-ML_BITS)
#define RUN_MASK ((1U<<RUN_BITS)-1)

// Return values
typedef enum {
    LZ4_OK = 0,              // Success code
    LZ4_INVALID_INPUT,       // Input file has an invalid format
    LZ4_BUFFER_TOO_SMALL     // Input or output file size is too large
} lz4_status;


/**
 * Perform the LZ4 compression on the DPU.
 *
 * @param input: holds input buffer information
 * @param output: holds output buffer information
 * @param block_size: size to compress at a time
 * @return LZ4_OK if successful, error code otherwise
 */
lz4_status dpu_compress(struct in_buffer_context *input, struct out_buffer_context *output, uint32_t block_size);

#endif

