/*
 * Outline of this program:
 * 	- CPU_compress: Uses a standard zlib routine to compress a stream
 * 	- CPU_decompress: Uses a standard zlib routine to decompress a zlib stream
 * 	- DPU_compress: Uses a custom routine running on the DPU to compress a stream into the zlib format
 * 	- DPU_decopmress: Uses a custom routine running on the DPU to decompress a zlib stream
 * 	
 * 	- main: Entry point for the host program.
 */


#include <dpu.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "zlib.h"

#define DPU_DECOMPRESS_PROGRAM "decompress.dpu"
#define DPU_COMPRESS_PROGRAM "compress.dpu"
#define CHUNK 16384

/*
 * This routine has been mostly taken from the zpipe.c
 */
int CPU_compress(FILE *source, FILE *dest, int level)
{
	// TODO: Implement this
	fprintf(stdout, "Not implemented yet");

	return 0;
}

/*
 * This routine has been mostly taken from the zpipe.c
 */
int CPU_decompress(FILE *source, FILE *dest)
{
	// TODO: Implement this
	fprintf(stdout, "Not implemented yet");

	return 0;
}

int DPU_compress(FILE *source, FILE *dest, int level)
{
	// TODO: Implement this
	fprintf(stdout, "Not implemented yet");

	return 0;
}

int DPU_decompress(FILE *source, FILE *dest, int level)
{
	// TODO: Implement this
	fprintf(stdout, "Not implemented yet");

	return 0;
}

/* report a zlib or i/o error */
void zerr(int ret)
{
    fputs("zpipe: ", stderr);
    switch (ret) {
    case Z_ERRNO:
        if (ferror(stdin))
            fputs("error reading stdin\n", stderr);
        if (ferror(stdout))
            fputs("error writing stdout\n", stderr);
        break;
    case Z_STREAM_ERROR:
        fputs("invalid compression level\n", stderr);
        break;
    case Z_DATA_ERROR:
        fputs("invalid or incomplete deflate data\n", stderr);
        break;
    case Z_MEM_ERROR:
        fputs("out of memory\n", stderr);
        break;
    case Z_VERSION_ERROR:
        fputs("zlib version mismatch!\n", stderr);
    }
}

int main(int argc, char **argv)
{
	char *usage = "usage: --cpu/--dpu --compress/--decompress <input> <output>\n";
	if (argc != 5) {
		fprintf(stderr, usage);
		return EXIT_FAILURE;
	}

	if (strcmp(argv[1], "--cpu") == 0) {
		if (strcmp(argv[2], "--compress") == 0) {
			// Prepare the cpu compression
		} else if (strcmp(argv[2], "--decompress") == 0) {
			// prepare the cpu decompression	
		} else {
			fprintf(stderr, "Unknown arguments");
			fprintf(stderr, usage);
		}
	} else if (strcmp(argv[1], "--dpu") == 0) {
		if (strcmp(argv[2], "--compress") == 0) {
			// Prepare the dpu compression
		} else if (strcmp(argv[2], "--decompress") == 0) {
			// prepare the dpu decompression	
		} else {
			fprintf(stderr, "Unknown arguments");
			fprintf(stderr, usage);
		}

	} else {
		fprintf(stderr, "Unknown arguments");
		fprintf(stderr, usage);
	}	

	fprintf(stdout, "Not implemented yet");
	return EXIT_SUCCESS;
}
