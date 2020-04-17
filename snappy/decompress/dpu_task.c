#include <mram.h>
#include <perfcounter.h>
#include <stdio.h>

#include "dpu_decompress.h"

#define BUF_SIZE (1 << 10)
#define HEAP_SIZE (1 << 12)

// MRAM variables
__host uint64_t compressed_length;
__host uint64_t uncompressed_length;
__host char compressed[BUF_SIZE];
__host char uncompressed[BUF_SIZE];

int main() {
    size_t result = 0;

    perfcounter_config(COUNT_CYCLES, true);

    // Do the uncompress.
    if (dpu_uncompress(compressed, compressed_length, uncompressed, &result)) {
        printf("Failed in %ld cycles\n", perfcounter_get());
        return -1;
    }

    uncompressed_length = result;
    printf("dpu result: %s\n", uncompressed);

    printf("Completed in %ld cycles\n", perfcounter_get());
    return 0;
}

