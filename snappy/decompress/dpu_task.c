#include <mram.h>
#include <perfcounter.h>
#include <stdio.h>
#include "alloc.h"
#include "dpu_decompress.h"

#define MAX_LENGTH 0x100000

// MRAM variables
__host uint32_t input_length;
__host uint32_t input_offset;
__host uint32_t output_length;
__host __mram_ptr char* input_buffer;
__host __mram_ptr char* output_buffer;

int main()
{
	struct in_buffer_context input;
	struct out_buffer_context output;

	perfcounter_config(COUNT_CYCLES, true);

	printf("DPU starting\n");

	if (input_length > MAX_LENGTH)
	{
		printf("Input length is too big: max=%i\n", MAX_LENGTH);
		return -2;
	}

	// prepare the descriptors
	input.length = input_length;
	output.buffer = output_buffer;
	output.append_ptr = (char*)ALIGN(mem_alloc(OUT_BUFFER_LENGTH), 8);
	output.read_ptr = (char*)ALIGN(mem_alloc(OUT_BUFFER_LENGTH), 8);
	output.curr = 0;
	output.append_window = 0;
	output.read_window = -1;
	output.length = output_length;
	output.flags = 0;

	printf("reading input data from MRAM @ 0x%x\n", (uint32_t)input_buffer);
	printf("writing output data to MRAM @ 0x%x\n", (uint32_t)output_buffer);

	// set up sequential reader which copies MRAM to WRAM on demand
	input.cache = seqread_alloc();
	//input.ptr = seqread_init(input.cache, DPU_MRAM_HEAP_POINTER, &input.sr);
	input.ptr = seqread_init(input.cache, input_buffer, &input.sr);
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

	printf("Completed in %ld cycles\n", perfcounter_get());
	return 0;
}

