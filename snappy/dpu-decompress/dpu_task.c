#include <mram.h>
#include <defs.h>
#include <perfcounter.h>
#include <stdio.h>
#include "alloc.h"
#include "dpu_decompress.h"

// MRAM variables
__host uint32_t input_length;
__host uint32_t output_length;
__host uint32_t input_offset[NR_TASKLETS];
__host uint32_t output_offset[NR_TASKLETS];
__host __mram_ptr uint8_t *input_buffer;
__host __mram_ptr uint8_t *output_buffer;

int main()
{
	struct in_buffer_context input;
	struct out_buffer_context output;
	uint8_t idx = me();

	printf("DPU starting, tasklet %d\n", idx);
	
	// Check that this tasklet has work to run 
	if ((idx != 0) && (input_offset[idx] == 0)) {
		printf("Tasklet %d has nothing to run\n", idx);
		return 0;
	}

	// Prepare the input and output descriptors
	uint32_t input_start = input_offset[idx] - input_offset[0];
	uint32_t output_start = output_offset[idx] - output_offset[0];

	input.cache = seqread_alloc();
	input.ptr = seqread_init(input.cache, input_buffer + input_start, &input.sr);
	input.curr = 0;
	input.length = 0;

	output.buffer = output_buffer + output_start;
	output.append_ptr = (uint8_t*)ALIGN(mem_alloc(OUT_BUFFER_LENGTH), 8);
	output.append_window = 0;
	output.read_ptr = (uint8_t*)ALIGN(mem_alloc(OUT_BUFFER_LENGTH), 8);
	output.read_window = -1;   
	output.curr = 0;
	output.length = 0;

	// Calculate the actual length this tasklet parses
	if (idx < (NR_TASKLETS - 1)) {
		int32_t input_end = input_offset[idx + 1] - input_offset[0];
		int32_t output_end = output_offset[idx + 1] - output_offset[0];

		// If the end position is negative, then the next task has no work
		// to run. Use the remainder of the input length to calculate this
		// task's length.
		if ((input_end <= 0) || (output_end <= 0)) {
			input.length = input_length - input_start;
			output.length = output_length - output_start;
		}
		else {
			input.length = input_end - input_start;
			output.length = output_end - output_start;
		}
	}
	else {
		input.length = input_length - input_start;
		output.length = output_length - output_start;
	} 
	
	perfcounter_config(COUNT_CYCLES, true);
	if (input.length != 0) {
		// Do the uncompress
		if (dpu_uncompress(&input, &output))
		{
			printf("Tasklet %d: failed in %ld cycles\n", idx, perfcounter_get());
			return -1;
		}
	}

	printf("Tasklet %d: completed in %ld cycles\n", idx, perfcounter_get());
	return 0;
}

