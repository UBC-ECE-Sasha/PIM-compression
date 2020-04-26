/**
 * DPU-compatible port of snappy decompression. Heavily borrowed from
 * https://github.com/andikleen/snappy-c
 */

#include <assert.h>
#include <string.h>  // memcpy
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "dpu_decompress.h"

/***********************
 * Struct declarations *
 ***********************/

/*******************
 * Memory helpers  *
 *******************/

static uint16_t make_offset_1_byte(unsigned char tag, struct buffer_context *input)
{
	if (input->curr >= input->buffer + input->length)
		return 0;
	return (uint16_t)((unsigned char)*input->curr++) | (uint16_t)(GET_OFFSET_1_BYTE(tag) << 8);
}

static uint16_t make_offset_2_byte(unsigned char tag, struct buffer_context *input)
{
	//printf("%s\n", __func__);
	unsigned char c;
	UNUSED(tag);
	uint16_t total=0;
	if (input->curr >= input->buffer + input->length)
		return 0;
	c = *input->curr++;
	total |= c;
	if (input->curr >= input->buffer + input->length)
		return 0;
	c = *input->curr++;
	return total | c << 8;
}

static uint32_t make_offset_4_byte(unsigned char tag, struct buffer_context *input)
{
	printf("%s\n", __func__);
	uint32_t total;
	UNUSED(tag);
	const char *limit = input->buffer + input->length;
	if (input->curr >= limit)
		return 0;
	total = *input->curr++;
	if (input->curr >= limit)
		return 0;
	total |= (*input->curr++) << 8;
	if (input->curr >= limit)
		return 0;
	total |= (*input->curr++) << 16;
	if (input->curr >= limit)
		return 0;
	return total | (*input->curr++) << 24;
}

/***************************
 * Reader & writer helpers *
 ***************************/

uint32_t read_long_literal_size(struct buffer_context *input, uint32_t len)
{
	uint32_t size = 0;
	int shift = 0;
	const char *limit = input->buffer + input->length;

	//printf("reading long literal in %u bytes\n", len);
	while (len--)
	{
		if (input->curr >= limit)
			return 0;
		char c = (*input->curr++);
		size |= c << shift;
		shift += 8;
	}

	return size;
}

static inline bool writer_append_dpu(struct buffer_context *input, struct buffer_context *output, uint32_t *len)
{
	//printf("Writing %u bytes\n", *len);
	while (*len &&
		input->curr < (input->buffer + input->length) &&
		output->curr < (output->buffer + output->length))
	{
		*output->curr = *input->curr;
		input->curr++;
		output->curr++;
		(*len) -= 1;
	}
	return true;
}

void write_copy_dpu(struct buffer_context *output, uint32_t copy_length, uint32_t offset)
{
	//printf("Copying %u bytes from offset=0x%lx to 0x%lx\n", copy_length, (output->curr - output->buffer) - offset, output->curr - output->buffer);
	const char *copy_curr = output->curr;
	copy_curr -= offset;
	if (copy_curr < output->buffer)
	{
		printf("bad offset!\n");
		return;
	}
	while (copy_length &&
		output->curr < (output->buffer + output->length))
	{
		*output->curr = *copy_curr;
		copy_curr++;
		output->curr++;
		copy_length -= 1;
	}
}

/**************************
 * Snappy decompressor.   *
 **************************/

/*********************
 * Public functions  *
 *********************/

snappy_status dpu_uncompress(struct buffer_context *input, struct buffer_context *output)
{
	while (input->curr < (input->buffer + input->length))
	{
		uint16_t length;
		uint32_t offset;
		const unsigned char tag = *input->curr++;
		dbg_printf("Got tag byte 0x%x at index 0x%x\n", tag, input->curr - input->buffer - 1);

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
			length = GET_LITERAL_LENGTH(tag) + 1;
			if (length > 60)
				length = read_long_literal_size(input, length - 60) + 1;

			uint32_t remaining = length;
			while (remaining &&
				input->curr < (input->buffer + input->length) &&
				output->curr < (output->buffer + output->length))
			{
				if (!writer_append_dpu(input, output, &remaining))
					return SNAPPY_OUTPUT_ERROR;
			}
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

	return SNAPPY_OK;
}

