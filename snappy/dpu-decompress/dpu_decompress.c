/**
 * DPU-compatible port of snappy decompression. Heavily borrowed from
 * https://github.com/andikleen/snappy-c
 */

#include <assert.h>
#include <string.h>  // memcpy
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <mram.h>
#include <defs.h>
#include "dpu_decompress.h"

/*******************
 * Memory helpers  *
 *******************/
static unsigned char READ_BYTE(struct in_buffer_context *_i)
{
	unsigned char ret = *_i->ptr;
	_i->ptr = seqread_get(_i->ptr, sizeof(uint8_t), &_i->sr);
	_i->curr++;
	return ret;
}

static uint16_t make_offset_1_byte(unsigned char tag, struct in_buffer_context *input)
{
	if (input->curr >= input->length)
		return 0;
	return (uint16_t)(READ_BYTE(input)) | (uint16_t)(GET_OFFSET_1_BYTE(tag) << 8);
}

static uint16_t make_offset_2_byte(unsigned char tag, struct in_buffer_context *input)
{
	UNUSED(tag);

	if ((input->curr + sizeof(uint16_t)) > input->length)
		return 0;
	   
	return (READ_BYTE(input) | (READ_BYTE(input) << 8));
}

static uint32_t make_offset_4_byte(unsigned char tag, struct in_buffer_context *input)
{
	if ((input->curr + sizeof(uint32_t)) > input->length)
		return 0;
	
	return (READ_BYTE(input) |
				(READ_BYTE(input) << 8) |
				(READ_BYTE(input) << 16) |
				(READ_BYTE(input) << 24));
}

/***************************
 * Reader & writer helpers *
 ***************************/
static bool read_length_dpu(struct in_buffer_context *input, uint32_t *len) 
{
	int shift = 0;
	*len = 0;

	for (uint8_t count = 0; count < 4; count++) {
		char c = READ_BYTE(input);
		*len |= (c & BITMASK(7)) << shift;

		if (!(c & (1 << 7)))
			return true;
		shift += 7;
	}
	return false;
}

uint32_t read_long_literal_size(struct in_buffer_context *input, uint32_t len)
{
	if ((input->curr + len) >= input->length)
		return 0;
	
	uint32_t size = 0;
	for (uint32_t i = 0; i < len; i++) {
		size |= (READ_BYTE(input) << (i << 3));
	}
	
	return size;
}

/**
 * Copy and append data from the input buffer to the output buffer.
 *
 * @param input: holds input buffer information
 * @param output: holds output buffer information
 * @param len: length of data to copy over
 * @return True
 */
static inline bool writer_append_dpu(struct in_buffer_context *input, struct out_buffer_context *output, uint16_t len)
{
	while (len)
	{
		uint32_t curr_index = output->curr - output->append_window;

		// if we are past the window, write the current window back to MRAM and start a new one
		if (curr_index >= OUT_BUFFER_LENGTH)
		{
			dbg_printf("Past EOB - writing back output %d\n", output->append_window);
			mram_write(output->append_ptr, &output->buffer[output->append_window], OUT_BUFFER_LENGTH);

			// if we are writing back the current append buffer, but also dependent on the append buffer
			// for the read window, we must keep a copy of the data for reading
			dbg_printf("Read window: 0x%x Append window: 0x%x\n", output->read_window, output->append_window);
			if (output->read_window == output->append_window)
				memcpy(output->read_ptr, output->append_ptr, OUT_BUFFER_LENGTH);

			output->append_window += OUT_BUFFER_LENGTH;
			curr_index = 0;
		}
		output->append_ptr[curr_index] = READ_BYTE(input);
		output->curr++;
		len--;
	}
	return true;
}

/**
 * Copy and append previous data to the output buffer. The data may
 * already be existing in the append buffer or read buffer in WRAM,
 * or may need to be copied into the read buffer first.
 *
 * @param output: holds output buffer information
 * @param copy_length: length of data to copy over
 * @param offset: where to copy from, offset from the current output
 *				  pointer
 */
void write_copy_dpu(struct out_buffer_context *output, uint32_t copy_length, uint32_t offset)
{
	// We only copy previous data, not future data
	if (offset > output->curr)
	{
		printf("Invalid offset detected: 0x%x\n", offset);
		return;
	}

	uint32_t read_index = output->curr - offset;
	dbg_printf("Copying %u bytes from offset=0x%x to 0x%x\n", copy_length, read_index, output->curr);

	// Load the correct read window and recalibrate the read index
	char *src_ptr = output->read_ptr;
	uint32_t need_window = WINDOW_ALIGN(read_index, OUT_BUFFER_LENGTH);
	read_index %= OUT_BUFFER_LENGTH;
	dbg_printf("Need window: 0x%x\n", need_window);
	dbg_printf("Have window: 0x%x\n", output->read_window);
	dbg_printf("Append window: 0x%x\n", output->append_window);

	if (need_window == output->append_window) // Use data currently in append window
		src_ptr = output->append_ptr;
	else if (need_window != output->read_window) // Need to load new read window
		mram_read(&output->buffer[need_window], output->read_ptr, OUT_BUFFER_LENGTH);
	// Else use the existing read window

	output->read_window = need_window;

	uint32_t curr_index = output->curr - output->append_window;
	while (copy_length)
	{
		// if we are past the append window, write the current window back to MRAM and start a new one
		if (curr_index >= OUT_BUFFER_LENGTH)
		{
			dbg_printf("Past EOB - writing back output %d\n", output->append_window);
			mram_write(output->append_ptr, &output->buffer[output->append_window], OUT_BUFFER_LENGTH);
			output->append_window += OUT_BUFFER_LENGTH;
			curr_index = 0;

			// if we are writing back the current append buffer, but also dependent on the append buffer
			// for the read window, we must keep a copy of the data for reading
			if (src_ptr == output->append_ptr)
			{
				memcpy(output->read_ptr, output->append_ptr, OUT_BUFFER_LENGTH);
				src_ptr = output->read_ptr;
			}
		}

		// if we are past the read window, load the next one
		if (read_index >= OUT_BUFFER_LENGTH)
		{
			dbg_printf("Past EORB - loading new read buffer 0x%x (old rb 0x%x)\n", need_window + OUT_BUFFER_LENGTH, need_window);
			need_window += OUT_BUFFER_LENGTH;

			// check to see if we are moving into the append window
			if (need_window == output->append_window)
			{
				src_ptr = output->append_ptr;
			}
			else
			{
				mram_read(&output->buffer[need_window], output->read_ptr, OUT_BUFFER_LENGTH);
				output->read_window = need_window;
				src_ptr = output->read_ptr;
			}
			read_index = 0;
		}
		
		output->append_ptr[curr_index++] = src_ptr[read_index++];
		output->curr++;
		copy_length--;
	}
}

/**************************
 * Snappy decompressor.   *
 **************************/

/*********************
 * Public functions  *
 *********************/

snappy_status dpu_uncompress(struct in_buffer_context *input, struct out_buffer_context *output)
{
	dbg_printf("curr: %u length: %u\n", input->curr, input->length);
	dbg_printf("output length: %u\n", output->length);
	while (input->curr < input->length) 
	{
		uint32_t compressed_block_size, uncompressed_block_size;
		
		// Read the compressed and uncompressed block size of current block
		read_length_dpu(input, &compressed_block_size);
		uint32_t end_block = input->curr + compressed_block_size;

		read_length_dpu(input, &uncompressed_block_size);
		
		while (input->curr < end_block) {
			uint32_t length;
			uint32_t offset;
			unsigned char tag;
			tag = READ_BYTE(input);
			dbg_printf("Got tag byte 0x%x at index 0x%x\n", tag, input->curr - 1);
			// There are two types of elements in a Snappy stream: Literals and
			// copies (backreferences). Each element starts with a tag byte,
			// and the lower two bits of this tag byte signal what type of element
			// will follow.
			switch (GET_ELEMENT_TYPE(tag))
			{
			case EL_TYPE_LITERAL:
				// For literals up to and including 60 bytes in length, the upper
				// six bits of the tag byte contain (len-1). The literal follows
				// immediately thereafter in the bytestream.
				length = GET_LENGTH_2_BYTE(tag) + 1;
				if (length > 60)
					length = read_long_literal_size(input, length - 60) + 1;
	
				if (!writer_append_dpu(input, output, length))
					return SNAPPY_OUTPUT_ERROR;
				break;

			// Copies are references back into previous decompressed data, telling
			// the decompressor to reuse data it has previously decoded.
			// They encode two values: The _offset_, saying how many bytes back
			// from the current position to read, and the _length_, how many bytes
			// to copy.
			case EL_TYPE_COPY_1:
				length = GET_LENGTH_1_BYTE(tag) + 4;
				offset = make_offset_1_byte(tag, input);
				write_copy_dpu(output, length, offset);
				break;

			case EL_TYPE_COPY_2:
				length = GET_LENGTH_2_BYTE(tag) + 1;
				offset = make_offset_2_byte(tag, input);
				write_copy_dpu(output, length, offset);
				break;

			case EL_TYPE_COPY_4:
				length = GET_LENGTH_2_BYTE(tag) + 1;
				offset = make_offset_4_byte(tag, input);
				write_copy_dpu(output, length, offset);
				break;
			}
		}
	}

	// Write out the final buffer
	if (output->append_window < output->length) {
		uint32_t len_final = output->length % OUT_BUFFER_LENGTH;
		if (len_final == 0)
			len_final = OUT_BUFFER_LENGTH;

		dbg_printf("Writing window at: 0x%x (%u bytes)\n", output->append_window, len_final);
		mram_write(output->append_ptr, &output->buffer[output->append_window], len_final);
	}
	return SNAPPY_OK;
}

