#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <getopt.h>

#include "dpu_snappy.h"
#include "snappy_compress.h"
#include "snappy_decompress.h"

const char options[]="dcb:i:o:";

/**
 * Read the contents of a file into an in-memory buffer. Upon success,
 * writes the amount read to input->length.
 *
 * @param in_file: input file name.
 * @param input: holds input buffer information
 * @return 1 if file does not exist, is too long, or different number of bytes
 *         were read than expected, 0 otherwise
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
		fprintf(stderr, "input_size is too big (%d > %ld)\n",
				input->length, input->max);
		return 1;
	}

	input->buffer = malloc(ALIGN(input->length, 8) * sizeof(*(input->buffer)));
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
 * @param out_file: output filename.
 * @param output: holds output buffer information
 */
static void write_output_host(char *out_file, struct host_buffer_context *output)
{
	FILE *fout = fopen(out_file, "w");
	fwrite(output->buffer, 1, output->length, fout);
	fclose(fout);
}

/**
 * Print out application usage.
 *
 * @param exe_name: name of the application
 */
static void usage(const char *exe_name)
{
#ifdef DEBUG
	fprintf(stderr, "**DEBUG BUILD**\n");
#endif //DEBUG
	fprintf(stderr, "Compress or decompress a file with Snappy\nCan use either the host CPU or UPMEM DPU\n");
	fprintf(stderr, "usage: %s [-d] [-c] [-b <block_size>] -i <input_file> [-o <output_file>]\n", exe_name);
	fprintf(stderr, "d: use DPU, by default host is used\n");
	fprintf(stderr, "c: perform compression, by default performs decompression\n");
	fprintf(stderr, "b: block size used for compression, default is 32KB, ignored for decompression\n");
	fprintf(stderr, "i: input file\n");
	fprintf(stderr, "o: output file\n");
}

/**
 * Calculate the difference between two timeval structs.
 */
double get_runtime(struct timeval *start, struct timeval *end) {
	double start_time = start->tv_sec + start->tv_usec / 1000000.0;
	double end_time = end->tv_sec + end->tv_usec / 1000000.0;
	return (end_time - start_time);
}

/**
 * Outputs the size of the decompressed snappy file.
 */
int main(int argc, char **argv)
{
	int opt;
	snappy_status status;
	
	int use_dpu = 0;
	int compress = 0;
	int block_size = 32 * 1024; // Default is 32KB
	char *input_file = NULL;
	char *output_file = NULL;
	struct host_buffer_context input;
	struct host_buffer_context output;

	input.buffer = NULL;
	input.length = 0;
	input.max = ULONG_MAX;

	output.buffer = NULL;
	output.length = 0;
	output.max = ULONG_MAX;

	while ((opt = getopt(argc, argv, options)) != -1)
	{
		switch(opt)
		{
		case 'd':
			use_dpu = 1;
			input.max = NR_DPUS * (unsigned long)MAX_FILE_LENGTH;
			output.max = NR_DPUS * (unsigned long)MAX_FILE_LENGTH;
			break;

		case 'c':
			compress = 1;
			break;
		
		case 'b':
			block_size = atoi(optarg);
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
		return -1;

	double preproc_time = 0.0;
	double alg_time = 0.0;
	double postproc_time = 0.0;

	if (compress) {
		setup_compression(&input, &output, &preproc_time);

		if (use_dpu)
		{
			status = snappy_compress_dpu(&input, &output, block_size, &preproc_time, &postproc_time);
		}
		else
		{
			struct timeval start;
			struct timeval end;

			gettimeofday(&start, NULL);	
			status = snappy_compress_host(&input, &output, block_size);
			gettimeofday(&end, NULL);

			alg_time = get_runtime(&start, &end);
		}
	}
	else {
		if (setup_decompression(&input, &output, &preproc_time))
			return -1;

		if (use_dpu)
		{
			status = snappy_decompress_dpu(&input, &output, &preproc_time, &postproc_time);
		}
		else
		{
			struct timeval start;
			struct timeval end;

			gettimeofday(&start, NULL);
			status = snappy_decompress_host(&input, &output);
			gettimeofday(&end, NULL);

			alg_time = get_runtime(&start, &end);
		}
	}
	
	if (status == SNAPPY_OK)
	{
		// Write the output buffer from main memory to a file
		write_output_host(output_file, &output);
		printf("%s %u bytes to: %s\n", (compress == 1) ? "Compressed" : "Decompressed", output.length, output_file);
	
		printf("Pre-processing time: %f\n", preproc_time);
		printf("Host time: %f\n", alg_time);
		printf("Post-processing time: %f\n", postproc_time);
	}
	else
	{
		fprintf(stderr, "Encountered Snappy error %u\n", status);
		return -1;
	}

	return 0;
}

