/**
 * Decompression on DPU.
 */

#ifndef _DPU_DECOMPRESS_H_
#define _DPU_DECOMPRESS_H_

#include "../dpu_snappy.h"
#include <seqread.h> // sequential reader

#ifdef DEBUG
#define dbg_printf(M, ...) printf("%s: " M , __func__, ##__VA_ARGS__)
#else
#define dbg_printf(...)
#endif

#define OUT_BUFFER_LENGTH 256

typedef struct in_buffer_context
{
	char* ptr;
	seqreader_buffer_t cache;
	seqreader_t sr;
	uint32_t curr;
	uint32_t length;
} in_buffer_context;

/* This is for buffering writes on UPMEM. The data is written in an 'append'
	mode to the "append window" in WRAM. This window holds a copy of a portion of
	the file that exists in MRAM. According to the 'snappy' algorithm, bytes that
	are appended to the output may be copies of previously written data. The data
	that must be copied is likely to not be contained by our small append window
	in WRAM, and therefore must be loaded into a second buffer - the read window.
	The read window holds a read-only copy of previous data from the file and it
	can point to any arbitrary (aligned) portion of previously written data. This
	simplifies memcpy from WRAM to MRAM.
*/
/* TODO: reduce the size of these variables, where possible */
typedef struct out_buffer_context
{
	__mram_ptr char* buffer; /* the entire output buffer in MRAM */
	char* append_ptr; /* the append window in WRAM */
	uint32_t append_window; /* offset of output buffer mapped by append window (must be multiple of window size) */
	char* read_ptr; /* the read window in WRAM */
	uint32_t read_window; /* offset of output buffer mapped by read window (must be multiple of window size) */
	uint32_t curr; /* current offset in output buffer in MRAM */
	uint32_t length; /* total size of output buffer in bytes */
} out_buffer_context;

/**
 * Uncompress a snappy compressed buffer. If successful, return 0 and write
 * the size of the uncompressed contents to uncompressed_length.
 * @param compressed Input buffer containing the compressed data.
 * @param length Length of the compressed buffer.
 * @param uncompressed Output buffer. This buffer should be at least as long
 *     as dpu_uncompressed_length(compressed).
 * @return 0 if successful.
 */
snappy_status dpu_uncompress(struct in_buffer_context *input, struct out_buffer_context *output);

#endif

