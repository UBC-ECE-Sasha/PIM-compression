#include <mram.h>
#include <perfcounter.h>
#include <stdio.h>
#include "alloc.h"

#include "dpu_decompress.h"

#define MAX_LENGTH 2048

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

	if (input_length > MAX_LENGTH)
	{
		printf("Input length is too big: max=%i\n", MAX_LENGTH);
		return -2;
	}

	// prepare the descriptors
	input.length = input_length;
	input.buffer = (char*)mem_alloc(MAX_LENGTH);
	input.curr = input.buffer + input_offset;
	output.length = output_length;
	output.buffer = (char*)ALIGN(mem_alloc(MAX_LENGTH), 8);
	output.curr = output.buffer;

	if ((unsigned int)input.buffer & 0x7)
	{
		printf("input buffer address is not aligned\n");
		return -1;
	}

	// copy the input buffer to WRAM
	dbg_printf("Copying %u from MRAM: 0x%x to WRAM: 0x%x\n", MAX_LENGTH, DPU_MRAM_HEAP_POINTER, (unsigned int)input.buffer);
	mram_read(DPU_MRAM_HEAP_POINTER, input.buffer, MAX_LENGTH);

	// Do the uncompress
	if (dpu_uncompress(&input, &output))
	{
		printf("Failed in %ld cycles\n", perfcounter_get());
		return -1;
	}

	// copy the input buffer to WRAM
	mram_write(output.buffer, DPU_MRAM_HEAP_POINTER, MAX_LENGTH);

	printf("Completed in %ld cycles\n", perfcounter_get());
	return 0;
}

