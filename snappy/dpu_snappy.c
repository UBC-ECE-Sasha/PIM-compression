#include "dpu_snappy.h"

#include <dpu.h>

#define DPU_DECOMPRESS_PROGRAM "decompress/decompress.dpu"

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
    // TODO
    (void) compressed;
    (void) compressed_length;
    (void) result;
    return SNAPPY_OK;
}

snappy_status snappy_validate_compressed_buffer(const char *compressed,
                                                size_t compressed_length) {
    // TODO
    (void) compressed;
    (void) compressed_length;
    return SNAPPY_OK;
}

int main() {
    // A main() function is needed to make this compile
    // TODO: is this avoidable? can dpu_snappy just be a library package?
    return 0;
}

