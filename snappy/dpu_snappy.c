#include <dpu.h>
#include <dpu_log.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <getopt.h>

#include "dpu_snappy.h"

#define DPU_DECOMPRESS_PROGRAM "decompress/decompress.dpu"
#define MAX_OUTPUT_LENGTH 16384

#define BUF_SIZE (1 << 10)
#define ALIGN(x) (x + 3) & ~3

#define BITMASK(_x) ((1 << _x) - 1)

#define GET_ELEMENT_TYPE(_tag) (_tag & BITMASK(2))
#define GET_LITERAL_LENGTH(_tag) (_tag >> 2)
#define GET_LENGTH_1_BYTE(_tag) ((_tag >> 2) & BITMASK(3))
#define GET_OFFSET_1_BYTE(_tag) ((_tag >> 5) & BITMASK(3))

#define GET_LENGTH_2_BYTE(_tag) ((_tag >> 2) & BITMASK(6))

enum element_type
{
	EL_TYPE_LITERAL,
	EL_TYPE_COPY_1,
	EL_TYPE_COPY_2,
	EL_TYPE_COPY_4
};

#define DEBUG 1

struct buffer_context
{
	char* buffer;
	char* curr;
	uint32_t length;
	uint32_t max;
};

/* Mapping from i in range [0,4] to a mask to extract the bottom 8*i bits
static const uint32_t wordmask[] = {
    0u, 0xffu, 0xffffu, 0xffffffu, 0xffffffffu
};

static const uint16_t char_table[256] = {
    0x0001, 0x0804, 0x1001, 0x2001, 0x0002, 0x0805, 0x1002, 0x2002,
    0x0003, 0x0806, 0x1003, 0x2003, 0x0004, 0x0807, 0x1004, 0x2004,
    0x0005, 0x0808, 0x1005, 0x2005, 0x0006, 0x0809, 0x1006, 0x2006,
    0x0007, 0x080a, 0x1007, 0x2007, 0x0008, 0x080b, 0x1008, 0x2008,
    0x0009, 0x0904, 0x1009, 0x2009, 0x000a, 0x0905, 0x100a, 0x200a,
    0x000b, 0x0906, 0x100b, 0x200b, 0x000c, 0x0907, 0x100c, 0x200c,
    0x000d, 0x0908, 0x100d, 0x200d, 0x000e, 0x0909, 0x100e, 0x200e,
    0x000f, 0x090a, 0x100f, 0x200f, 0x0010, 0x090b, 0x1010, 0x2010,
    0x0011, 0x0a04, 0x1011, 0x2011, 0x0012, 0x0a05, 0x1012, 0x2012,
    0x0013, 0x0a06, 0x1013, 0x2013, 0x0014, 0x0a07, 0x1014, 0x2014,
    0x0015, 0x0a08, 0x1015, 0x2015, 0x0016, 0x0a09, 0x1016, 0x2016,
    0x0017, 0x0a0a, 0x1017, 0x2017, 0x0018, 0x0a0b, 0x1018, 0x2018,
    0x0019, 0x0b04, 0x1019, 0x2019, 0x001a, 0x0b05, 0x101a, 0x201a,
    0x001b, 0x0b06, 0x101b, 0x201b, 0x001c, 0x0b07, 0x101c, 0x201c,
    0x001d, 0x0b08, 0x101d, 0x201d, 0x001e, 0x0b09, 0x101e, 0x201e,
    0x001f, 0x0b0a, 0x101f, 0x201f, 0x0020, 0x0b0b, 0x1020, 0x2020,
    0x0021, 0x0c04, 0x1021, 0x2021, 0x0022, 0x0c05, 0x1022, 0x2022,
    0x0023, 0x0c06, 0x1023, 0x2023, 0x0024, 0x0c07, 0x1024, 0x2024,
    0x0025, 0x0c08, 0x1025, 0x2025, 0x0026, 0x0c09, 0x1026, 0x2026,
    0x0027, 0x0c0a, 0x1027, 0x2027, 0x0028, 0x0c0b, 0x1028, 0x2028,
    0x0029, 0x0d04, 0x1029, 0x2029, 0x002a, 0x0d05, 0x102a, 0x202a,
    0x002b, 0x0d06, 0x102b, 0x202b, 0x002c, 0x0d07, 0x102c, 0x202c,
    0x002d, 0x0d08, 0x102d, 0x202d, 0x002e, 0x0d09, 0x102e, 0x202e,
    0x002f, 0x0d0a, 0x102f, 0x202f, 0x0030, 0x0d0b, 0x1030, 0x2030,
    0x0031, 0x0e04, 0x1031, 0x2031, 0x0032, 0x0e05, 0x1032, 0x2032,
    0x0033, 0x0e06, 0x1033, 0x2033, 0x0034, 0x0e07, 0x1034, 0x2034,
    0x0035, 0x0e08, 0x1035, 0x2035, 0x0036, 0x0e09, 0x1036, 0x2036,
    0x0037, 0x0e0a, 0x1037, 0x2037, 0x0038, 0x0e0b, 0x1038, 0x2038,
    0x0039, 0x0f04, 0x1039, 0x2039, 0x003a, 0x0f05, 0x103a, 0x203a,
    0x003b, 0x0f06, 0x103b, 0x203b, 0x003c, 0x0f07, 0x103c, 0x203c,
    0x0801, 0x0f08, 0x103d, 0x203d, 0x1001, 0x0f09, 0x103e, 0x203e,
    0x1801, 0x0f0a, 0x103f, 0x203f, 0x2001, 0x0f0b, 0x1040, 0x2040
};
*/

const char options[]="i:o:";

static uint16_t make_offset_1_byte(unsigned char tag, struct buffer_context *input)
{
	//printf("%s\n", __func__);
	if (input->curr >= input->buffer + input->length)
		return 0;
	return (uint16_t)((unsigned char)*input->curr++) | (uint16_t)(GET_OFFSET_1_BYTE(tag) << 8);
}

static uint16_t make_offset_2_byte(unsigned char tag, struct buffer_context *input)
{
	//printf("%s\n", __func__);
	unsigned char c;
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

static int read_input_host(char *in_file, struct buffer_context *input)
{
	size_t input_size;
    FILE *fin = fopen(in_file, "r");
    fseek(fin, 0, SEEK_END);
    input_size = ftell(fin);
    fseek(fin, 0, SEEK_SET);

    if (input->length > input->max) {
        fprintf(stderr, "input_size is too big (%d > %d)\n",
                input->length, input->max);
        return 1;
    }

	input->length = input_size;
	input->buffer = malloc(input->length);
	input->curr = input->buffer;
	size_t n = fread(input->buffer, 1, input->length, fin);
	fclose(fin);

#if DEBUG
    printf("%s: read %d bytes from %s (%lu)\n", __func__, input->length, in_file, n);
#endif

   return (n != input->length);
}

static inline bool writer_append_host(struct buffer_context *input, struct buffer_context *output, uint32_t *len)
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

void write_copy_host(struct buffer_context *output, uint32_t copy_length, uint32_t offset)
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
bool read_uncompressed_length_host(struct buffer_context *input, uint32_t *len)
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

void decompress_all_tags_host(struct buffer_context *input, struct buffer_context *output)
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
					return;
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
}

snappy_status snappy_uncompress_host(struct buffer_context *input, struct buffer_context *output)
{
	//fprintf(stderr, "%s\n", __func__);
	uint32_t uncompressed_length;
	if (!read_uncompressed_length_host(input, &uncompressed_length))
	{
		printf("read uncompressed length failed\n");
      return SNAPPY_BUFFER_TOO_SMALL;
	}
	//printf("Uncompressed len: 0x%x=%u\n", uncompressed_length, uncompressed_length);

	if (uncompressed_length > output->max)
		return -SNAPPY_BUFFER_TOO_SMALL;

	output->buffer = malloc(uncompressed_length);
	output->curr = output->buffer;
	output->length = uncompressed_length;
	decompress_all_tags_host(input, output);
/*
	exit_snappy_decompressor(&decompressor);

    // Check that decompressor reached EOF, and output reached the end
	if (!decompressor.eof)
	{
		printf("Not EOF\n");
		return -EIO;
	}

	if (writer->op != writer->op_limit)
	{
		printf("Not op limit 0x%x != 0x%x\n", (unsigned int)writer->op, (unsigned int)writer->op_limit);
		return -EIO;
	}
*/
	return SNAPPY_OK;
}

snappy_status snappy_compress(const char* input,
                              size_t input_length,
                              char* compressed,
                              size_t *compressed_length) {
    // TODO use host system snappy
    (void) input;
    (void) input_length;
    (void) compressed;
    (void) compressed_length;
    return SNAPPY_OK;
}

snappy_status snappy_uncompress(const char* compressed,
                                size_t compressed_length,
                                char* uncompressed,
                                size_t* uncompressed_length) {
    struct dpu_set_t dpus;
    struct dpu_set_t dpu;
    uint64_t res_size=0;
	uint32_t stage=0;

    DPU_ASSERT(dpu_alloc(1, NULL, &dpus));

    DPU_FOREACH(dpus, dpu) {
        break;
    }

    DPU_ASSERT(dpu_load(dpu, DPU_DECOMPRESS_PROGRAM, NULL));
    DPU_ASSERT(dpu_copy_to(dpu, "compressed_length", 0, &compressed_length, sizeof(compressed_length)));
   DPU_ASSERT(dpu_copy_to(dpu, "compressed", 0, compressed, ALIGN(compressed_length)));
    dpu_launch(dpu, DPU_SYNCHRONOUS);

    DPU_ASSERT(dpu_copy_from(dpu, "stage", 0, &stage, sizeof(stage)));
    DPU_ASSERT(dpu_copy_from(dpu, "uncompressed_length", 0, &res_size, sizeof(res_size)));

	printf("Finished stage %u\n", stage);
    // Uncompressed size might be too big to read back to host.
    if (res_size > BUF_SIZE) {
        fprintf(stderr, "uncompressed file is too big (%ld > %d)\n", 
                res_size, BUF_SIZE);
        exit(EXIT_FAILURE);
    }

    DPU_ASSERT(dpu_copy_from(dpu, "uncompressed", 0, uncompressed, ALIGN(res_size)));

    DPU_FOREACH(dpus, dpu) {
        DPU_ASSERT(dpu_log_read(dpu, stdout));
    }

    DPU_ASSERT(dpu_free(dpus));

    *uncompressed_length = res_size;
    return SNAPPY_OK;
}

size_t snappy_max_compressed_length(size_t source_length) {
    // TODO
    (void) source_length;
    return 0;
}

snappy_status snappy_uncompressed_length(const char *compressed,
                                         size_t compressed_length,
                                         size_t *result)
{
    struct dpu_set_t dpus;
    struct dpu_set_t dpu;
    uint64_t res_size;

    DPU_ASSERT(dpu_alloc(1, NULL, &dpus));

    DPU_FOREACH(dpus, dpu) {
        break;
    }

    DPU_ASSERT(dpu_load(dpu, DPU_DECOMPRESS_PROGRAM, NULL));
    DPU_ASSERT(dpu_copy_to(dpu, "compressed_length", 0, &compressed_length, 
                           sizeof(compressed_length)));
    DPU_ASSERT(dpu_copy_to(dpu, "compressed", 0, compressed, 
                          (compressed_length + 3) & ~3));
    DPU_ASSERT(dpu_launch(dpu, DPU_SYNCHRONOUS));

    DPU_ASSERT(dpu_copy_from(dpu, "uncompressed_length", 0, &res_size, 
                             sizeof(res_size)));

    DPU_FOREACH(dpus, dpu) {
        DPU_ASSERT(dpu_log_read(dpu, stdout));
    }

    DPU_ASSERT(dpu_free(dpus));

    *result = res_size;
    return SNAPPY_OK;
}

snappy_status snappy_validate_compressed_buffer(const char *compressed,
                                                size_t compressed_length) {
    // TODO
    (void) compressed;
    (void) compressed_length;
    return SNAPPY_OK;
}

/**
 * Read the contents of a file into an in-memory buffer. Upon success,
 * return 0 and write the amount read to input_size.
 * @param input The input filename.
 * @param input_buf The buffer to write the contents to.
 * @param input_size Size of input.
 */
static int read_input(char *in_file, char *input_buf, size_t *input_size) {
    FILE *fin = fopen(in_file, "r");
    fseek(fin, 0, SEEK_END);
    *input_size = ftell(fin);
    fseek(fin, 0, SEEK_SET);

    if (*input_size > BUF_SIZE) {
        fprintf(stderr, "input_size is too big (%lu > %d)\n",
                *input_size, BUF_SIZE);
        return 1;
    }

#if DEBUG
    printf("read_input: read %lu bytes from %s\n", *input_size, in_file);
#endif
   
    size_t n = fread(input_buf, sizeof(*input_buf), *input_size, fin);
    (void) n;
    fclose(fin);

    return 0;
}

static int write_output_host(char *out_file, struct buffer_context* output)
{
    FILE *fout = fopen(out_file, "w");
    fwrite(output->buffer, 1, output->length, fout);
    fclose(fout);

    return 0;
}

/**
 * Write the contents of the output buffer to a file.
 * @param output The output filename.
 * @param output_buf Pointer to the buffer containing the contents.
 * @param output_size Size of buffer contents.
 */
static int write_output(char *output, char *output_buf, size_t output_size) {
    FILE *fout = fopen(output, "w");
    fwrite(output_buf, sizeof(*output_buf), output_size, fout);
    fclose(fout);

    return 0;
}

static int get_uncompressed_length(char *input) {
    char compressed[BUF_SIZE];

    size_t compressed_len;
    if (read_input(input, compressed, &compressed_len)) {
        return 1;
    }

    size_t result;
    snappy_status status = snappy_uncompressed_length(compressed, 
                                                      compressed_len, 
                                                      &result);
    if (status != SNAPPY_OK) {
        fprintf(stderr, "encountered snappy error\n");
        exit(EXIT_FAILURE);
    }

    printf("snappy_uncompressed_length: %ld\n", result);
    return 0;
}

static int uncompress(char *in_file, char *output) {
    char compressed[BUF_SIZE];
    char uncompressed[BUF_SIZE];

    size_t compressed_len;
    if (read_input(in_file, compressed, &compressed_len)) {
        return 1;
    }

    size_t uncompressed_len = 0;
	printf("snappy_uncompress\n");
    snappy_status status = snappy_uncompress(compressed, compressed_len,
                                             uncompressed, &uncompressed_len);
    if (status != SNAPPY_OK) {
        fprintf(stderr, "encountered snappy error\n");
        exit(EXIT_FAILURE);
    }

    printf("host result: %s\n", uncompressed);

    if (write_output(output, uncompressed, uncompressed_len)) {
        return 1;
    }

    printf("uncompressed %lu bytes to: %s\n", uncompressed_len, output);
    return 0;
}

static void usage(const char* exe_name)
{
	fprintf(stderr, "usage: %s -i <compressed_input> (-o <output>)\n", exe_name);
}

/**
 * Outputs the size of the decompressed snappy file.
 */
int main(int argc, char **argv)
{
	int opt;
	int use_dpu=0;
	char *input_file=NULL;
	char *output_file=NULL;
	struct buffer_context input;
	struct buffer_context output;

	input.buffer = NULL;
	input.length = 0;
	input.max = 65535;

	output.buffer = NULL;
	output.length = 0;
	output.max = MAX_OUTPUT_LENGTH;

	printf("Getting options\n");
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

	printf("done\n");
	if (!input_file)
	{
		usage(argv[0]);
		return -1;
	}

	printf("Using input file %s\n", input_file);
	if (output_file)
		printf("Using output file %s\n", output_file);

	if (output_file)
	{
		// read the input file into main memory
		if (read_input_host(input_file, &input))
			return 1;

		if (use_dpu)
		{
			uncompress(input_file, output_file);
		}
		else
		{
			snappy_status status = snappy_uncompress_host(&input, &output);
			if (status != SNAPPY_OK) {
				fprintf(stderr, "encountered snappy error\n");
				exit(EXIT_FAILURE);
			}
		}

		// write the output buffer from main memory to a file
		if (write_output_host(output_file, &output))
			return 1;

		printf("uncompressed %u bytes to: %s\n", output.length, output_file);
	}
	else
	{
		get_uncompressed_length(input_file);
	}

	return 0;
}

