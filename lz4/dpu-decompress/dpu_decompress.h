/**
 * Decompression on DPU.
 */

#ifndef _DPU_DECOMPRESS_H_
#define _DPU_DECOMPRESS_H_

#include "common.h"
#include <seqread.h> // sequential reader

#define GET_ELEMENT_TYPE(_tag)  (_tag & BITMASK(2))
#define GET_LENGTH_1_uint8_t(_tag) ((_tag >> 2) & BITMASK(3))
#define GET_OFFSET_1_uint8_t(_tag) ((_tag >> 5) & BITMASK(3))
#define GET_LENGTH_2_uint8_t(_tag) ((_tag >> 2) & BITMASK(6))

// Length of the "append window" and "read window" in the
// out_buffer_context
#define OUT_BUFFER_LENGTH 256

#define FASTLOOP_SAFE_DISTANCE 64

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

// Sequential reader cache size must be the same as the
// append window size, since we memcpy from one to the other
#undef SEQREAD_CACHE_SIZE
#define SEQREAD_CACHE_SIZE OUT_BUFFER_LENGTH

// Return values
typedef enum {
    LZ4_OK = 0,              // Success code
	LZ4_ERROR = 1,
	LZ4_INVALID_INPUT,       // Input file has an invalid format
    LZ4_BUFFER_TOO_SMALL     // Input or output file size is too large
} lz4_status;

// LZ4 tag types
enum element_type
{
    EL_TYPE_LITERAL,
    EL_TYPE_COPY_1,
    EL_TYPE_COPY_2,
    EL_TYPE_COPY_4
};

typedef struct in_buffer_context
{
	uint8_t *ptr;
	seqreader_buffer_t cache;
	seqreader_t sr;
	uint32_t curr;
	uint32_t length;
} in_buffer_context;

/**
 * This is for buffering writes on UPMEM. The data is written in an 'append'
 * mode to the "append window" in WRAM. This window holds a copy of a portion of
 * the file that exists in MRAM. According to the 'lz4' algorithm, bytes that
 * are appended to the output may be copies of previously written data. The data
 * that must be copied is likely to not be contained by our small append window
 * in WRAM, and therefore must be loaded into a second buffer - the read window.
 * The read window holds a read-only copy of previous data from the file and it
 * can point to any arbitrary (aligned) portion of previously written data. This
 * simplifies memcpy from WRAM to MRAM.
 *
 * TODO: reduce the size of these variables, where possible 
 */
typedef struct out_buffer_context
{
	__mram_ptr uint8_t *buffer; /* the entire output buffer in MRAM */
	uint8_t *append_ptr; /* the append window in WRAM */
	uint32_t append_window; /* offset of output buffer mapped by append window (must be multiple of window size) */
	uint8_t *read_buf;
	uint32_t curr; /* current offset in output buffer in MRAM */
	uint32_t length; /* total size of output buffer in bytes */
} out_buffer_context;

/**
 * Perform the LZ4 decompression on the DPU.
 *
 * @param input: holds input buffer information
 * @param output: holds output buffer information
 * @return LZ4_OK if successful, error code otherwise
 */
lz4_status dpu_uncompress(struct in_buffer_context *input, struct out_buffer_context *output);

#endif

