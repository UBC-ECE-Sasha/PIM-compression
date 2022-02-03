/**
 * Compression on DPU.
 */

#ifndef _DPU_COMPRESS_H_
#define _DPU_COMPRESS_H_

#include "common.h"
#include <seqread.h>

// Length of the "append window" in out_buffer_context
#define OUT_BUFFER_LENGTH 256

// Sequential reader cache size must be the same as the
// append window size, since we memcpy from one to the other
#undef SEQREAD_CACHE_SIZE
#define SEQREAD_CACHE_SIZE OUT_BUFFER_LENGTH


/* Types */
typedef uint8_t BYTE;
typedef uint16_t U16;
typedef uint32_t U32;
typedef uint64_t U64;
typedef uintptr_t uptrval;

typedef U64 reg_t;

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
    SNAPPY_OK = 0,              // Success code
    SNAPPY_INVALID_INPUT,       // Input file has an invalid format
    SNAPPY_BUFFER_TOO_SMALL     // Input or output file size is too large
} snappy_status;


typedef struct in_buffer_context
{
	__mram_ptr uint8_t *buffer;
	uint8_t *ptr;
	seqreader_buffer_t cache;
	seqreader_t sr;
	uint32_t curr;
	uint32_t length;
} in_buffer_context;

typedef struct out_buffer_context
{
	__mram_ptr uint8_t *buffer; // Entire buffer in MRAM
	uint8_t *append_ptr; 		// Append window in MRAM
	uint32_t append_window;		// Offset of output buffer mapped by append window
	uint32_t curr;				// Current offset in output buffer in MRAM
	uint32_t length;			// Total size of output buffer in bytes
} out_buffer_context;

/**
 * Perform the Snappy compression on the DPU.
 *
 * @param input: holds input buffer information
 * @param output: holds output buffer information
 * @param block_size: size to compress at a time
 * @return SNAPPY_OK if successful, error code otherwise
 */
snappy_status dpu_compress(struct in_buffer_context *input, struct out_buffer_context *output, uint32_t block_size);

#endif

