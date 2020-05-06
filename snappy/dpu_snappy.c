#include <dpu.h>
#include <dpu_memory.h>
#include <dpu_log.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <getopt.h>

#include "dpu_snappy.h"

#define DPU_DECOMPRESS_PROGRAM "decompress/decompress.dpu"
#define MAX_OUTPUT_LENGTH 16384

#define BUF_SIZE (1 << 10)

const char options[]="di:o:";

static bool read_uncompressed_length_host(struct host_buffer_context *input, uint32_t *len)
{
	int shift = 0;
	const char *limit = input->buffer + input->length;

	*len = 0;
	while (1)
	{
		if (input->curr >= limit)
			return false;
		char c = (*input->curr++);
		*len |= (c & BITMASK(7)) << shift;
		if (!(c & (1 << 7)))
			return true;
		shift += 7;
		if (shift > 32)
			return false;
	}

	return true;
}

static snappy_status setup_output_descriptor(struct host_buffer_context *input, struct host_buffer_context *output)
{
	uint32_t uncompressed_length;
	if (!read_uncompressed_length_host(input, &uncompressed_length))
	{
		printf("read uncompressed length failed\n");
        return SNAPPY_BUFFER_TOO_SMALL;
	}

	if (uncompressed_length > output->max)
		return -SNAPPY_BUFFER_TOO_SMALL;

	output->buffer = malloc(uncompressed_length | BITMASK(11));
	output->curr = output->buffer;
	output->length = uncompressed_length;

	return SNAPPY_OK;
}

static uint16_t make_offset_1_byte(unsigned char tag, struct host_buffer_context *input)
{
	//printf("%s\n", __func__);
	if (input->curr >= input->buffer + input->length)
		return 0;
	return (uint16_t)((unsigned char)*input->curr++) | (uint16_t)(GET_OFFSET_1_BYTE(tag) << 8);
}

static uint16_t make_offset_2_byte(unsigned char tag, struct host_buffer_context *input)
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

static uint32_t make_offset_4_byte(unsigned char tag, struct host_buffer_context *input)
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

static inline bool writer_append_host(struct host_buffer_context *input, struct host_buffer_context *output, uint32_t *len)
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

void write_copy_host(struct host_buffer_context *output, uint32_t copy_length, uint32_t offset)
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

uint32_t read_long_literal_size(struct host_buffer_context *input, uint32_t len)
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

snappy_status snappy_uncompress_host(struct host_buffer_context *input, struct host_buffer_context *output)
{
	while (input->curr < (input->buffer + input->length))
	{
		uint16_t length;
		uint32_t offset;
		const unsigned char tag = *input->curr++;
		//printf("Got tag byte 0x%x at index 0x%lx\n", tag, input->curr - input->buffer - 1);

	    /* There are two types of elements in a Snappy stream: Literals and
		copies (backreferences). Each element starts with a tag byte,
		and the lower two bits of this tag byte signal what type of element
		will follow. */
		switch (GET_ELEMENT_TYPE(tag))
		{
		case EL_TYPE_LITERAL:
			/* For literals up to and including 60 bytes in length, the upper
				six bits of the tag byte contain (len-1). The literal follows
				immediately thereafter in the bytestream. */
			length = GET_LITERAL_LENGTH(tag) + 1;
			//printf("reading literal length=%u\n", length);

			if (length > 60)
			{
				length = read_long_literal_size(input, length - 60) + 1;
				//printf("reading literal length=%u\n", length);
			}

			uint32_t remaining = length;
			while (remaining &&
				input->curr < (input->buffer + input->length) &&
				output->curr < (output->buffer + output->length))
			{
				if (!writer_append_host(input, output, &remaining))
					return SNAPPY_OUTPUT_ERROR;
			}
			break;

			/* Copies are references back into previous decompressed data, telling
				the decompressor to reuse data it has previously decoded.
				They encode two values: The _offset_, saying how many bytes back
				from the current position to read, and the _length_, how many bytes
				to copy. */
		case EL_TYPE_COPY_1:
			length = GET_LENGTH_1_BYTE(tag) + 4;
			offset = make_offset_1_byte(tag, input);
			write_copy_host(output, length, offset);
			break;

		case EL_TYPE_COPY_2:
			length = GET_LENGTH_2_BYTE(tag) + 1;
			offset = make_offset_2_byte(tag, input);
			write_copy_host(output, length, offset);
			break;

		case EL_TYPE_COPY_4:
			length = GET_LENGTH_2_BYTE(tag) + 1;
			offset = make_offset_4_byte(tag, input);
			write_copy_host(output, length, offset);
			break;
		}
	}

	return SNAPPY_OK;
}

/**
 * Prepare the DPU context by copying the buffer to be decompressed and
 * uploading the program to the DPU.
 */
snappy_status snappy_uncompress_dpu(struct host_buffer_context *input, struct host_buffer_context *output)
{
	struct dpu_set_t dpus;
	struct dpu_set_t dpu;
	uint64_t res_size = 0;

	UNUSED(output);

    // Allocate a DPU
	DPU_ASSERT(dpu_alloc(1, NULL, &dpus));

	DPU_FOREACH(dpus, dpu) {
		break;
	}

	// Set up and run the program on the DPU
	uint32_t input_buffer_start = 1024 * 1024;
	uint32_t output_buffer_start = ALIGN(input_buffer_start + input->length + 64, 64);
	uint32_t offset = (uint32_t)(input->curr - input->buffer);

    // Must be a multiple of 8 to ensure the last write to MRAM is also a multiple of 8
    uint32_t output_length = (output->length + 7) & ~7;

	DPU_ASSERT(dpu_load(dpu, DPU_DECOMPRESS_PROGRAM, NULL));
	DPU_ASSERT(dpu_copy_to(dpu, "input_length", 0, &input->length, sizeof(uint32_t)));
	DPU_ASSERT(dpu_copy_to(dpu, "input_buffer", 0, &input_buffer_start, sizeof(uint32_t)));
	DPU_ASSERT(dpu_copy_to(dpu, "input_offset", 0, &offset, sizeof(uint32_t)));
	DPU_ASSERT(dpu_copy_to(dpu, "output_length", 0, &output_length, sizeof(uint32_t)));
	DPU_ASSERT(dpu_copy_to(dpu, "output_buffer", 0, &output_buffer_start, sizeof(uint32_t)));
	dpu_copy_to_mram(dpu.dpu, input_buffer_start, (unsigned char*)input->buffer, input->length, 0);
	
    int ret = dpu_launch(dpu, DPU_SYNCHRONOUS);
	if (ret != 0)
	{
		DPU_ASSERT(dpu_free(dpus));
		return SNAPPY_INVALID_INPUT;
	}

	// Get the results back from the DPU 
	dpu_copy_from_mram(dpu.dpu, (unsigned char*)output->buffer, output_buffer_start, output->length, 0);

	// Uncompressed size might be too big to read back to host
	if (res_size > BUF_SIZE) {
		printf("uncompressed file is too big (%ld > %d)\n", res_size, BUF_SIZE);
		exit(EXIT_FAILURE);
	}

    // Deallocate the DPUs
	DPU_FOREACH(dpus, dpu) {
		DPU_ASSERT(dpu_log_read(dpu, stdout));
	}

	DPU_ASSERT(dpu_free(dpus));

	return SNAPPY_OK;
}

/**
 * Read the contents of a file into an in-memory buffer. Upon success,
 * return 0 and write the amount read to input->length.
 *
 * @param in_file The input filename.
 * @param input The struct to which contents of file are written to.
 */
static int read_input_host(char *in_file, struct host_buffer_context *input)
{
    FILE *fin = fopen(in_file, "r");
    if (fin == NULL) {
        fprintf(stderr, "Invalid input file: %s\n", in_file);
        return 1;
    }

    fseek(fin, 0, SEEK_END);
    input->length = ftell(fin);
    fseek(fin, 0, SEEK_SET);

    if (input->length > input->max) {
        fprintf(stderr, "input_size is too big (%d > %d)\n",
                input->length, input->max);
        return 1;
    }

	input->buffer = malloc(input->length * sizeof(*(input->buffer)));
	input->curr = input->buffer;
	size_t n = fread(input->buffer, sizeof(*(input->buffer)), input->length, fin);
	fclose(fin);

#ifdef DEBUG
    printf("%s: read %d bytes from %s (%lu)\n", __func__, input->length, in_file, n);
#endif

   return (n != input->length);
}

/**
 * Write the contents of the output buffer to a file.
 *
 * @param out_file The output filename.
 * @param output Pointer to the buffer containing the contents.
 */
static int write_output_host(char *out_file, struct host_buffer_context* output)
{
    FILE *fout = fopen(out_file, "w");
    fwrite(output->buffer, 1, output->length, fout);
    fclose(fout);

    return 0;
}

static void usage(const char* exe_name)
{
#ifdef DEBUG
	fprintf(stderr, "**DEBUG BUILD**\n");
#endif //DEBUG
	fprintf(stderr, "Decompress a file compressed with snappy\nCan use either the host CPU or UPMEM DPU\n");
	fprintf(stderr, "usage: %s -d -i <compressed_input> (-o <output>)\n", exe_name);
	fprintf(stderr, "d: use DPU\n");
	fprintf(stderr, "i: input file\n");
	fprintf(stderr, "o: output file\n");
}

/**
 * Outputs the size of the decompressed snappy file.
 */
int main(int argc, char **argv)
{
	int opt;
	int use_dpu = 0;
    snappy_status status;
	char *input_file = NULL;
	char *output_file = NULL;
	struct host_buffer_context input;
	struct host_buffer_context output;

	input.buffer = NULL;
	input.length = 0;
	input.max = 65535;

	output.buffer = NULL;
	output.length = 0;
	output.max = MAX_OUTPUT_LENGTH;

	while ((opt = getopt(argc, argv, options)) != -1)
	{
		switch(opt)
		{
		case 'd':
			use_dpu = 1;
			break;

		case 'i':
			input_file = optarg;
			break;

		case 'o':
			output_file = optarg;
			break;

		default:
			usage(argv[0]);
			return -2;
		}
	}

	if (!input_file)
	{
		usage(argv[0]);
		return -1;
	}
	printf("Using input file %s\n", input_file);

    // If no output file was provided, use a default file
    if (output_file == NULL) {
        output_file = "output.txt";
    }
    printf("Using output file %s\n", output_file);

	// Read the input file into main memory
	if (read_input_host(input_file, &input))
		return 1;

	status = setup_output_descriptor(&input, &output);

	if (use_dpu)
	{
		status = snappy_uncompress_dpu(&input, &output);
	}
	else
	{
		status = snappy_uncompress_host(&input, &output);
	}

	if (status == SNAPPY_OK)
	{
		// Write the output buffer from main memory to a file
	    if (write_output_host(output_file, &output))
		    return 1;

		printf("uncompressed %u bytes to: %s\n", output.length, output_file);
	}
	else
	{
		fprintf(stderr, "encountered snappy error %u\n", status);
		exit(EXIT_FAILURE);
	}

	return 0;
}

