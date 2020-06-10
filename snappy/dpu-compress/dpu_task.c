#include <mram.h>
#include <defs.h>
#include <perfcounter.h>
#include <stdio.h>
#include "alloc.h"
#include "dpu_compress.h"

// MRAM variables
__host uint32_t block_size;
__host uint32_t input_length;
__host uint32_t output_length;
__host __mram_ptr uint8_t *input_buffer;
__host __mram_ptr uint8_t *output_buffer;

__mram_ptr uint8_t *input_buf;

int main()
{
	struct in_buffer_context input;
	struct out_buffer_context output;
	uint8_t idx = me();

	printf("DPU starting, tasklet %d\n", idx);

	input_buf = input_buffer;
	input.cache = seqread_alloc();
	input.ptr = seqread_init(input.cache, input_buffer, &input.sr);
	input.curr = 0;
	input.length = input_length;

	output.buffer = output_buffer;
	output.append_ptr = (uint8_t*)ALIGN(mem_alloc(OUT_BUFFER_LENGTH), 8);
	output.append_window = 0;
	output.curr = 0;
	output.length = output_length;
	
	perfcounter_config(COUNT_CYCLES, true);
	if (input.length != 0) {
		// Do the uncompress
		if (dpu_compress(&input, &output, block_size))
		{
			printf("Tasklet %d: failed in %ld cycles\n", idx, perfcounter_get());
			return -1;
		}
		output_length = output.length;
	}

	printf("Tasklet %d: completed in %ld cycles\n", idx, perfcounter_get());
	return 0;
}

