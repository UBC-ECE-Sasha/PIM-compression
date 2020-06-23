/**
 * Decompression on DPU.
 */

#ifndef _DPU_DECOMPRESS_H_
#define _DPU_DECOMPRESS_H_

#include "common.h"
#include <seqread.h> // sequential reader

#define GET_ELEMENT_TYPE(_tag)  (_tag & BITMASK(2))
#define GET_LENGTH_1_BYTE(_tag) ((_tag >> 2) & BITMASK(3))
#define GET_OFFSET_1_BYTE(_tag) ((_tag >> 5) & BITMASK(3))
#define GET_LENGTH_2_BYTE(_tag) ((_tag >> 2) & BITMASK(6))

// Length of the "append window" and "read window" in the
// out_buffer_context
#define OUT_BUFFER_LENGTH 256

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
 * the file that exists in MRAM. According to the 'snappy' algorithm, bytes that
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
	uint8_t *read_ptr; /* the read window in WRAM */
	uint32_t read_window; /* offset of output buffer mapped by read window (must be multiple of window size) */
	uint32_t curr; /* current offset in output buffer in MRAM */
	uint32_t length; /* total size of output buffer in bytes */
} out_buffer_context;

/**
 * Perform the Snappy decompression on the DPU.
 *
 * @param input: holds input buffer information
 * @param output: holds output buffer information
 * @return SNAPPY_OK if successful, error code otherwise
 */
snappy_status dpu_uncompress(struct in_buffer_context *input, struct out_buffer_context *output);

#endif

