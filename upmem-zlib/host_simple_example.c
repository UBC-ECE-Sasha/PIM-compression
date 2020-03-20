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
#include "common.h"

#define DPU_DECOMPRESS_PROGRAM "./decompress.dpu"
/*#define DPU_DECOMPRESS_PROGRAM "./helloworld"*/
#define DPU_COMPRESS_PROGRAM "compress.dpu"
#define CHUNK 16384

/*
 * This routine has been mostly taken from the zpipe.c
 */
int CPU_compress(FILE *source, FILE *dest, int level)
{
	int ret, flush;
	unsigned have;
	z_stream strm;
	unsigned char in[CHUNK];
	unsigned char out[CHUNK];

	/* allocate deflate state */
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	ret = deflateInit(&strm, level);
	if (ret != Z_OK)
		return ret;

	/* compress until end of file */
	do {
		strm.avail_in = fread(in, 1, CHUNK, source);
		if (ferror(source)) {
			(void)deflateEnd(&strm);
			return Z_ERRNO;
		}
		flush = feof(source) ? Z_FINISH : Z_NO_FLUSH;
		strm.next_in = in;

		/* run deflate() on input until output buffer not full, finish
		   compression if all of source has been read in */
		do {
			strm.avail_out = CHUNK;
			strm.next_out = out;
			ret = deflate(&strm, flush);    /* no bad return value */
			assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
			have = CHUNK - strm.avail_out;
			if (fwrite(out, 1, have, dest) != have || ferror(dest)) {
				(void)deflateEnd(&strm);
				return Z_ERRNO;
			}
		} while (strm.avail_out == 0);
		assert(strm.avail_in == 0);     /* all input will be used */

		/* done when last data in file processed */
	} while (flush != Z_FINISH);
	assert(ret == Z_STREAM_END);        /* stream will be complete */

	/* clean up and return */
	(void)deflateEnd(&strm);
	return Z_OK;
}

/*
 * This routine has been mostly taken from the zpipe.c
 */
int CPU_decompress(FILE *source, FILE *dest)
{
	int ret;
	unsigned have;
	z_stream strm;
	unsigned char in[CHUNK];
	unsigned char out[CHUNK];

	/* allocate inflate state */
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	strm.avail_in = 0;
	strm.next_in = Z_NULL;
	ret = inflateInit(&strm);
	if (ret != Z_OK)
		return ret;

	/* decompress until deflate stream ends or end of file */
	do {
		strm.avail_in = fread(in, 1, CHUNK, source);
		if (ferror(source)) {
			(void)inflateEnd(&strm);
			return Z_ERRNO;
		}
		if (strm.avail_in == 0)
			break;
		strm.next_in = in;

		/* run inflate() on input until output buffer not full */
		do {
		    strm.avail_out = CHUNK;
		    strm.next_out = out;
		    ret = inflate(&strm, Z_NO_FLUSH);
		    assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
		    switch (ret) {
			    case Z_NEED_DICT:
                    ret = Z_DATA_ERROR;     /* and fall through */
			    case Z_DATA_ERROR:
			    case Z_MEM_ERROR:
                    (void)inflateEnd(&strm);
                    return ret;
		    }
		    have = CHUNK - strm.avail_out;
		    if (fwrite(out, 1, have, dest) != have || ferror(dest)) {
                (void)inflateEnd(&strm);
                return Z_ERRNO;
		    }
		} while (strm.avail_out == 0);

		/* done when inflate() says it's done */
	} while (ret != Z_STREAM_END);

	/* clean up and return */
	(void)inflateEnd(&strm);
	return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;

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
    
    struct dpu_set_t dpus, dpu;
    uint32_t res_size;

    DPU_ASSERT(dpu_alloc(1, NULL, &dpus));
    int ret = dpu_load(dpus, DPU_DECOMPRESS_PROGRAM, NULL);
    fprintf(stdout, "Result of dpu_load: %d\n", ret);
    DPU_ASSERT(ret);

    // Get the source file size
    uint32_t file_size = 0;
    if (fseek(source, 0L, SEEK_END) == 0) {
        file_size = ftell(source);

        if (fseek(source, 0L, SEEK_SET) != 0) {
            fprintf(stderr, "Error occurred while reading file\n");
            return EXIT_FAILURE;
        }
    }

    if (file_size == 0) {
        fprintf(stderr, "Unable to read source file, is it empty?\n");
    } else if (file_size > DPU_CACHE_SIZE) {
        // TODO: Use MRAM instead of WRAM
        fprintf(stderr, "File size too big for DPU cache at the moment. Max size is 16K\n");
        return EXIT_FAILURE;
    }

    // Copy source data into a buffer
    uint8_t src_buf[DPU_CACHE_SIZE];
    size_t bytes_read = fread(src_buf, sizeof(char), file_size, source);
    if (bytes_read != file_size) {
        fprintf(stderr, "Error reading file into host buffer\n");
        return EXIT_FAILURE;
    }
    

    // copy data to DPU 
    DPU_ASSERT(dpu_copy_to(dpus, "input_size", 0, &file_size, sizeof(file_size)));
    DPU_ASSERT(dpu_copy_to(dpus, "input", 0, src_buf, DPU_CACHE_SIZE));
    DPU_ASSERT(dpu_launch(dpus, DPU_SYNCHRONOUS));

    DPU_FOREACH(dpus, dpu) {
        DPU_ASSERT(dpu_log_read(dpu, stdout));
        break;
    }

    // copy data from DPU
    uint32_t result_size;
    DPU_ASSERT(dpu_copy_from(dpu, "result_size", 0, &result_size, sizeof(result_size)));
    uint8_t result_buf[result_size];
    DPU_ASSERT(dpu_copy_from(dpu, "output", 0, result_buf, result_size));
    int32_t dpu_ret;
    DPU_ASSERT(dpu_copy_from(dpu, "ret", 0, &dpu_ret, sizeof(dpu_ret)));

    // write data to output file
    size_t bytes_written = fwrite(result_buf, 1, result_size, dest);
    if (bytes_written != result_size) {
        fprintf(stderr, "Failed to write to destination file\n");
        return EXIT_FAILURE;
    }
    
    DPU_ASSERT(dpu_free(dpus));

	fprintf(stdout, "Not implemented yet\n");

	return dpu_ret;
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

	FILE *fin = fopen(argv[3], "r");
	if (fin == NULL) {
		fprintf(stderr, "Invalid input file");
		return EXIT_FAILURE;
	}

	FILE *fout = fopen(argv[4], "w");
	if (fout == NULL) {
		fprintf(stderr, "Invalid output file");
		return EXIT_FAILURE;
	}

	int ret;

	if (strcmp(argv[1], "--cpu") == 0) {
		if (strcmp(argv[2], "--compress") == 0) {
			// Prepare the cpu compression
			ret = CPU_compress(fin, fout, Z_DEFAULT_COMPRESSION);
			if (ret != Z_OK)
				zerr(ret);
			return ret;			
		} else if (strcmp(argv[2], "--decompress") == 0) {
			// prepare the cpu decompression	
			ret = CPU_decompress(fin, fout);
			if (ret != Z_OK)
				zerr(ret);
			return ret;
		} else {
			fprintf(stderr, "Unknown arguments");
			fprintf(stderr, usage);
		}
	} else if (strcmp(argv[1], "--dpu") == 0) {
		if (strcmp(argv[2], "--compress") == 0) {
			// Prepare the dpu compression
			ret = DPU_compress(fin, fout, Z_DEFAULT_COMPRESSION);
			if (ret != Z_OK)
				zerr(ret);
			return ret;
		} else if (strcmp(argv[2], "--decompress") == 0) {
			// prepare the dpu decompression	
			ret = DPU_decompress(fin, fout, Z_DEFAULT_COMPRESSION);
			if (ret != Z_OK)
				zerr(ret);
			return ret;
		} else {
			fprintf(stderr, "Unknown arguments");
			fprintf(stderr, usage);
		}

	} else {
		fprintf(stderr, "Unknown arguments");
		fprintf(stderr, usage);
	}	

	return EXIT_FAILURE;
}
