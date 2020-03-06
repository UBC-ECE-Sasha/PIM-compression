#include <dpu.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#include "dpu_snappy.h"

#define DPU_DECOMPRESS_PROGRAM "decompress/decompress.dpu"

#define BUF_SIZE (1 << 10)

static char compressed[BUF_SIZE];

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
    // TODO use DPU_DECOMPRESS_PROGRAM
    (void) compressed;
    (void) compressed_length;
    (void) uncompressed;
    (void) uncompressed_length;
    return SNAPPY_OK;
}

size_t snappy_max_compressed_length(size_t source_length) {
    // TODO
    (void) source_length;
    return 0;
}

snappy_status snappy_uncompressed_length(const char *compressed,
                                         size_t compressed_length,
                                         size_t *result) {
    struct dpu_set_t dpus;
    struct dpu_set_t dpu;
    uint64_t res_size;

    DPU_ASSERT(dpu_alloc(1, NULL, &dpus));

    DPU_FOREACH(dpus, dpu) {
        break;
    }

    DPU_ASSERT(dpu_load(dpu, DPU_DECOMPRESS_PROGRAM, NULL));
    DPU_ASSERT(dpu_copy_to(dpu, "compressed_length", 0, &compressed_length, sizeof(compressed_length)));
    DPU_ASSERT(dpu_copy_to(dpu, "compressed", 0, compressed, (compressed_length + 3) & ~3));
    DPU_ASSERT(dpu_launch(dpu, DPU_SYNCHRONOUS));

    DPU_ASSERT(dpu_copy_from(dpu, "uncompressed_length", 0, &res_size, sizeof(res_size)));

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
 * Outputs the size of the decompressed snappy file.
 */
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <compressed_input>\n", argv[0]);
        return EXIT_FAILURE;
    }

    char *input = argv[1];

    FILE *fin = fopen(input, "r");
    fseek(fin, 0, SEEK_END);
    uint32_t input_size = ftell(fin);
    fseek(fin, 0, SEEK_SET);

    if (input_size > BUF_SIZE) {
        fprintf(stderr, "input_size is too big (%d > %d)\n",
                input_size, BUF_SIZE);
        exit(EXIT_FAILURE);
    }

    size_t n_read = fread(compressed, sizeof(*compressed), input_size, fin);
    (void) n_read;
    fclose(fin);

    size_t uncompressed_size = 0;
    snappy_status status = snappy_uncompressed_length(compressed, input_size, 
                                                      &uncompressed_size);
    if (status != SNAPPY_OK) {
        fprintf(stderr, "encountered snappy error\n");
        exit(EXIT_FAILURE);
    }

    fprintf(stderr, "snappy_uncompressed_length: %ld\n", uncompressed_size);
    return 0;
}

