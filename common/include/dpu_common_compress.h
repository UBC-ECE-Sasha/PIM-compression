#include "common.h"
#include <seqread.h>

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
