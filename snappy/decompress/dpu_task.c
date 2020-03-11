#include <mram.h>
#include <perfcounter.h>
#include <stdio.h>

#include "dpu_decompress.h"

#define BUF_SIZE (1 << 10)

__host uint64_t compressed_length;
__host uint64_t uncompressed_length;
__host char compressed[BUF_SIZE];
__host char uncompressed[BUF_SIZE];

int main() {
    size_t result = 0;

    perfcounter_config(COUNT_CYCLES, true);

    // Check size of uncompressed result.
    if (dpu_uncompressed_length(compressed, compressed_length, &result)) {
        printf("Failed in %ld cycles\n", perfcounter_get());
        return -1;
    }

    uncompressed_length = result;

    // Do the uncompress.
    if (dpu_uncompress(compressed, compressed_length, uncompressed)) {
        printf("Failed in %ld cycles\n", perfcounter_get());
        return -1;
    }

    printf("Completed in %ld cycles\n", perfcounter_get());
    return 0;
}

