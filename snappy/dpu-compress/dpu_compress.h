/**
 * Compression on DPU.
 */

#ifndef _DPU_COMPRESS_H_
#define _DPU_COMPRESS_H_

#include "../dpu_snappy.h"
#include <seqread.h>

// Length of the "append window" and "read window" in the
// out_buffer_context
#define OUT_BUFFER_LENGTH 256

extern __mram_ptr uint8_t *input_buf;

typedef struct in_buffer_context
{
	uint8_t *ptr;
	seqreader_buffer_t cache;
	seqreader_t sr;
	uint32_t curr;
	uint32_t length;
} in_buffer_context;

typedef struct out_buffer_context
{
	__mram_ptr uint8_t *buffer; // Entire buffer in MRAM
	uint8_t *append_ptr;
	uint32_t append_window;
	uint32_t curr;
	uint32_t length;
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

