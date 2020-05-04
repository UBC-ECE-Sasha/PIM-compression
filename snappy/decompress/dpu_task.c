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
	struct in_buffer_context input;
	struct buffer_context output;

	perfcounter_config(COUNT_CYCLES, true);

	if (input_length > MAX_LENGTH)
	{
		printf("Input length is too big: max=%i\n", MAX_LENGTH);
		return -2;
	}

	// prepare the descriptors
	input.length = input_length;
	output.length = output_length;
	output.buffer = (char*)ALIGN(mem_alloc(MAX_LENGTH), 8);
	output.curr = output.buffer;

	// set up sequential reader which copies MRAM to WRAM on demand
	printf("Sequential reader size: %u\n", SEQREAD_CACHE_SIZE);
	input.cache = seqread_alloc();
	dbg_printf("input cache: 0x%x\n", input.cache);
	input.ptr = seqread_init(input.cache, DPU_MRAM_HEAP_POINTER, &input.sr);
	dbg_printf("input ptr: 0x%x\n", input.ptr);
	input.curr = 0;

	// fast-forward sequential reader to account for bytes already read
	while (++input.curr < input_offset)
		input.ptr = seqread_get(input.ptr, sizeof(uint8_t), &input.sr);

	// Do the uncompress
	if (dpu_uncompress(&input, &output))
	{
		printf("Failed in %ld cycles\n", perfcounter_get());
		return -1;
	}

	// copy the output buffer to WRAM
	mram_write(output.buffer, DPU_MRAM_HEAP_POINTER, MAX_LENGTH);

	printf("Completed in %ld cycles\n", perfcounter_get());
	return 0;
}

