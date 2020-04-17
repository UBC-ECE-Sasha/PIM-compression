#include <mram.h>
#include <perfcounter.h>
#include <stdio.h>
#include "alloc.h"

#include "dpu_decompress.h"

#define BUF_SIZE (1 << 10)
#define HEAP_SIZE (1 << 12)

// MRAM variables
__host uint32_t input_length;
__host char* input_buffer;
__host uint32_t input_offset;
__host uint32_t output_length;
__host char* output_buffer;

int main()
{
	struct buffer_context input;
	struct buffer_context output;

	perfcounter_config(COUNT_CYCLES, true);


	// prepare the descriptors
	input.length = input_length;
	input.buffer = (char*)mem_alloc(2048);
	input.curr = input.buffer + input_offset;
	output.length = output_length;
	output.buffer = (char*)mem_alloc(2048);
	output.curr = output.buffer;

	// copy the input buffer to WRAM
	MRAM_READ(DPU_MRAM_HEAP_POINTER, input.buffer, 2048);

	// Do the uncompress.
	if (dpu_uncompress(&input, &output))
	{
		printf("Failed in %ld cycles\n", perfcounter_get());
		return -1;
	}

	// copy the input buffer to WRAM
	MRAM_WRITE(output.buffer, DPU_MRAM_HEAP_POINTER, 2048);

	printf("Completed in %ld cycles\n", perfcounter_get());
	return 0;
}

