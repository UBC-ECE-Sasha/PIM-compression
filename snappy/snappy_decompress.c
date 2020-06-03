#include <dpu.h>
#include <dpu_memory.h>
#include <dpu_log.h>
#include <stdint.h>
#include <stdio.h>

#include "snappy_decompress.h"

#define DPU_DECOMPRESS_PROGRAM "dpu-decompress/decompress.dpu"
#define TOTAL_NR_TASKLETS (NR_DPUS * NR_TASKLETS)

/**
 * Attempt to read a varint from the input buffer. The format of a varint
 * consists of little-endian series of bytes where the lower 7 bits are data
 * and the upper bit is set if there are more bytes to read. Maximum size
 * of the varint is 5 bytes.
 *
 * @param input: holds input buffer information
 * @param val: read value of the varint
 * @return False if all 5 bytes were read and there is still more data to
 *		   read, True otherwise
 */
static inline bool read_varint32(struct host_buffer_context *input, uint32_t *val)
{
	int shift = 0;
	*val = 0;

	for (uint8_t count = 0; count < 5; count++) {
		int8_t c = (int8_t)(*input->curr++);
		*val |= (c & BITMASK(7)) << shift;
		if (!(c & (1 << 7)))
			return true;
		shift += 7;
	}

	return false;
}

/**
 * Read the size of the long literal tag, which is used for literals with
 * length greater than 60 bytes.
 *
 * @param input: holds input buffer information
 * @param len: length in bytes of the size to read
 * @return 0 if we reached the end of input buffer, size of literal otherwise
 */
static inline uint32_t read_long_literal_size(struct host_buffer_context *input, uint32_t len)
{
	if ((input->curr + len) >= (input->buffer + input->length))
		return 0;

	uint32_t size = 0;
	for (uint32_t i = 0; i < len; i++) {
		size |= (*input->curr++ << (i << 3));
	}
	return size;
}

/**
 * Read a 1-byte offset tag and return the offset of the copy that is read.
 *
 * @param tag: tag byte to parse
 * @param input: holds input buffer information
 * @return 0 if we reached the end of input buffer, offset of the copy otherwise
 */
static inline uint16_t make_offset_1_byte(uint8_t tag, struct host_buffer_context *input)
{
	if (input->curr >= (input->buffer + input->length))
		return 0;
	return (uint16_t)(*input->curr++) | (uint16_t)(GET_OFFSET_1_BYTE(tag) << 8);
}

/**
 * Read a 2-byte offset tag and return the offset of the copy that is read.
 *
 * @param tag: tag byte to parse
 * @param input: holds input buffer information
 * @return 0 if we reached the end of input buffer, offset of the copy otherwise
 */
static inline uint16_t make_offset_2_byte(uint8_t tag, struct host_buffer_context *input)
{
	UNUSED(tag);

	uint16_t total = 0;
	if ((input->curr + sizeof(uint16_t)) > (input->buffer + input->length))
		return 0;
	else {
		total = (*input->curr & 0xFF) | ((*(input->curr + 1) & 0xFF) << 8);
		input->curr += sizeof(uint16_t);
		return total;
	}
}

/**
 * Read a 4-byte offset tag and return the offset of the copy that is read.
 *
 * @param tag: tag byte to parse
 * @param input: holds input buffer information
 * @return 0 if we reached the end of input buffer, offset of the copy otherwise
 */
static inline uint32_t make_offset_4_byte(uint8_t tag, struct host_buffer_context *input)
{
	UNUSED(tag);

	uint32_t total = 0;
	if ((input->curr + sizeof(uint32_t)) > (input->buffer + input->length))
		return 0;
	else {
		total = (*input->curr & 0xFF) |
				((*(input->curr + 1) & 0xFF) << 8) |
				((*(input->curr + 2) & 0xFF) << 16) |
			 ((*(input->curr + 3) & 0xFF) << 24);
		input->curr += sizeof(uint32_t);
		return total;
	}
}

/**
 * Copy and append data from the input bufer to the output buffer.
 *
 * @param input: holds input buffer information
 * @param output: holds output buffer information
 * @param len: length of data to copy over
 */
static void writer_append_host(struct host_buffer_context *input, struct host_buffer_context *output, uint32_t len)
{
	//printf("Writing %u bytes at 0x%x\n", len, (input->curr - input->buffer));
	while (len &&
		(input->curr < (input->buffer + input->length)) &&
		(output->curr < (output->buffer + output->length)))
	{
		*output->curr = *input->curr;
		input->curr++;
		output->curr++;
		len--;
	}
}

/**
 * Copy and append previously uncompressed data to the output buffer.
 *
 * @param output: holds output buffer information
 * @param copy_length: length of data to copy over
 * @param offset: where to copy from, offset from current output pointer
 * @return False if offset if invalid, True otherwise
 */
static bool write_copy_host(struct host_buffer_context *output, uint32_t copy_length, uint32_t offset)
{
	//printf("Copying %u bytes from offset=0x%lx to 0x%lx\n", copy_length, (output->curr - output->buffer) - offset, output->curr - output->buffer);
	const uint8_t *copy_curr = output->curr;
	copy_curr -= offset;
	if (copy_curr < output->buffer)
	{
		printf("bad offset!\n");
		return false;
	}
	while (copy_length &&
		output->curr < (output->buffer + output->length))
	{
		*output->curr = *copy_curr;
		copy_curr++;
		output->curr++;
		copy_length -= 1;
	}

	return true;
}


snappy_status setup_decompression(struct host_buffer_context *input, struct host_buffer_context *output, uint32_t input_offset[NR_DPUS][NR_TASKLETS], uint32_t output_offset[NR_DPUS][NR_TASKLETS])
{
	uint32_t dpu_idx = 0;
	uint32_t task_idx = 0;

	// Read the decompressed length
	uint32_t dlength;
	if (!read_varint32(input, &dlength)) {
		fprintf(stderr, "Failed to read decompressed length\n");
		return SNAPPY_INVALID_INPUT;
	}

	// Read the decompressed block size
	uint32_t dblock_size;
	if (!read_varint32(input, &dblock_size)) {
		fprintf(stderr, "Failed to read decompressed block size\n");
		return SNAPPY_INVALID_INPUT;
	}

	uint32_t num_blocks = (dlength + dblock_size - 1) / dblock_size;
	uint32_t input_blocks_per_dpu = (num_blocks + NR_DPUS - 1) / NR_DPUS;
	uint32_t input_blocks_per_task = (num_blocks + TOTAL_NR_TASKLETS - 1) / TOTAL_NR_TASKLETS;

	uint32_t task_blocks = 0;
	uint32_t total_offset = 0;
	for (uint32_t i = 0; i < num_blocks; i++) {
		// If we have reached the next DPU's boundary, update the index
		if (i == (input_blocks_per_dpu * (dpu_idx + 1))) {
			dpu_idx++;
			task_idx = 0;
			task_blocks = 0;
		}

		// If we have reached the next task's boundary, log the offset
		// to the input_offset and output_offset arrays. This should roughly
		// evenly divide the work between NR_TASKLETS tasks on NR_DPUS.
		if (task_blocks == (input_blocks_per_task * task_idx)) {
			input_offset[dpu_idx][task_idx] = total_offset;
			output_offset[dpu_idx][task_idx] = i * dblock_size;
			task_idx++;
		}

		// Read the compressed block size
		total_offset += (*input->curr & 0xFF) |
				((*(input->curr + 1) & 0xFF) << 8) |
				((*(input->curr + 2) & 0xFF) << 16) |
				((*(input->curr + 3) & 0xFF) << 24);
		task_blocks++;
		input->curr += sizeof(uint32_t);
	}

	// Check that uncompressed length is within the max we can store
	if (dlength > output->max) {
		fprintf(stderr, "Output length is to big: max=%ld len=%d\n", output->max, dlength);
		return SNAPPY_BUFFER_TOO_SMALL;
	}

	// Allocate output buffer
	output->buffer = malloc(ALIGN(dlength, 8) | BITMASK(11));
	output->curr = output->buffer;
	output->length = dlength;

	return SNAPPY_OK;
}


snappy_status snappy_decompress_host(struct host_buffer_context *input, struct host_buffer_context *output)
{
	while (input->curr < (input->buffer + input->length)) {
		uint16_t length;
		uint32_t offset;
		const uint8_t tag = *input->curr++;
		//printf("Got tag byte 0x%x at index 0x%lx\n", tag, input->curr - input->buffer - 1);

		/* There are two types of elements in a Snappy stream: Literals and
		copies (backreferences). Each element starts with a tag byte,
		and the lower two bits of this tag byte signal what type of element
		will follow. */
		switch (GET_ELEMENT_TYPE(tag))
		{
		case EL_TYPE_LITERAL:
			/* For literals up to and including 60 bytes in length, the upper
			 * six bits of the tag byte contain (len-1). The literal follows
			 * immediately thereafter in the bytestream.
			 */
			length = GET_LENGTH_2_BYTE(tag) + 1;
			if (length > 60)
			{
				length = read_long_literal_size(input, length - 60) + 1;
			}

			writer_append_host(input, output, length);
			break;

		/* Copies are references back into previous decompressed data, telling
		 * the decompressor to reuse data it has previously decoded.
		 * They encode two values: The _offset_, saying how many bytes back
		 * from the current position to read, and the _length_, how many bytes
		 * to copy.
		 */
		case EL_TYPE_COPY_1:
			length = GET_LENGTH_1_BYTE(tag) + 4;
			offset = make_offset_1_byte(tag, input);
			if (!write_copy_host(output, length, offset))
				return SNAPPY_INVALID_INPUT;
			break;

		case EL_TYPE_COPY_2:
			length = GET_LENGTH_2_BYTE(tag) + 1;
			offset = make_offset_2_byte(tag, input);
			if (!write_copy_host(output, length, offset))
				return SNAPPY_INVALID_INPUT;
			break;

		case EL_TYPE_COPY_4:
			length = GET_LENGTH_2_BYTE(tag) + 1;
			offset = make_offset_4_byte(tag, input);
			if (!write_copy_host(output, length, offset))
				return SNAPPY_INVALID_INPUT;
			break;
		}
	}

	return SNAPPY_OK;
}


snappy_status snappy_decompress_dpu(struct host_buffer_context *input, struct host_buffer_context *output, uint32_t input_offset[NR_DPUS][NR_TASKLETS], uint32_t output_offset[NR_DPUS][NR_TASKLETS])
{
	struct dpu_set_t dpus;
	struct dpu_set_t dpu;

	// Allocate a DPU
	 DPU_ASSERT(dpu_alloc(NR_DPUS, NULL, &dpus));

	// Calculate input length without header and aligned output length
	uint32_t total_input_length = input->length - (input->curr - input->buffer);
	uint32_t aligned_output_length = ALIGN(output->length, 8);

	uint32_t dpu_idx = 0;
	uint32_t input_length;
	uint32_t output_length;

	uint32_t input_buffer_start = 1024 * 1024;
	uint32_t output_buffer_start;
	DPU_FOREACH(dpus, dpu) {
		// Calculate input and output lengths for each DPU
		if ((dpu_idx != (NR_DPUS - 1)) && (input_offset[dpu_idx + 1][0] != 0)) {
			input_length = input_offset[dpu_idx + 1][0] - input_offset[dpu_idx][0];
			output_length = output_offset[dpu_idx + 1][0] - output_offset[dpu_idx][0];
		}
		else if ((dpu_idx == 0) || (input_offset[dpu_idx][0] != 0)) {
			input_length = total_input_length - input_offset[dpu_idx][0];
			output_length = aligned_output_length - output_offset[dpu_idx][0];
		}
		else {
			input_length = 0;
			output_length = 0;
		}

		// Calculate starting buffer positions in MRAM
		output_buffer_start = ALIGN(input_buffer_start + input_length + 64, 64);

		// Set up and load the DPU program
		DPU_ASSERT(dpu_load(dpu, DPU_DECOMPRESS_PROGRAM, NULL));
		DPU_ASSERT(dpu_copy_to(dpu, "input_length", 0, &input_length, sizeof(uint32_t)));
		DPU_ASSERT(dpu_copy_to(dpu, "input_buffer", 0, &input_buffer_start, sizeof(uint32_t)));
		DPU_ASSERT(dpu_copy_to(dpu, "input_offset", 0, input_offset[dpu_idx], sizeof(uint32_t) * NR_TASKLETS));
		DPU_ASSERT(dpu_copy_to(dpu, "output_offset", 0, output_offset[dpu_idx], sizeof(uint32_t) * NR_TASKLETS));
		DPU_ASSERT(dpu_copy_to(dpu, "output_length", 0, &output_length, sizeof(uint32_t)));
		DPU_ASSERT(dpu_copy_to(dpu, "output_buffer", 0, &output_buffer_start, sizeof(uint32_t)));
		DPU_ASSERT(dpu_copy_to_mram(dpu.dpu, input_buffer_start, input->curr + input_offset[dpu_idx][0], ALIGN(input_length, 8), 0));

		dpu_idx++;
	}

	// Launch all DPUs
	int ret = dpu_launch(dpus, DPU_SYNCHRONOUS);
	if (ret != 0)
	{
		DPU_ASSERT(dpu_free(dpus));
		return SNAPPY_INVALID_INPUT;
	}

	// Deallocate the DPUs
	dpu_idx = 0;
	DPU_FOREACH(dpus, dpu) {
		// Get the results back from the DPU
		DPU_ASSERT(dpu_copy_from(dpu, "input_length", 0, &input_length, sizeof(uint32_t)));
		DPU_ASSERT(dpu_copy_from(dpu, "output_length", 0, &output_length, sizeof(uint32_t)));

		output_buffer_start = ALIGN(input_buffer_start + input_length + 64, 64);
		DPU_ASSERT(dpu_copy_from_mram(dpu.dpu, output->buffer + output_offset[dpu_idx][0], output_buffer_start, output_length, 0));

		printf("------DPU %d Logs------\n", dpu_idx);
		DPU_ASSERT(dpu_log_read(dpu, stdout));

		dpu_idx++;
	}

	DPU_ASSERT(dpu_free(dpus));

	return SNAPPY_OK;
}	