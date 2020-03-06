#include <mram.h>
#include <perfcounter.h>
#include <stdio.h>

#include "dpu_decompress.h"

#define BUF_SIZE (1 << 10)

__host uint64_t compressed_length;
__host uint64_t uncompressed_length;
__host char compressed[BUF_SIZE];

int main() {
    perfcounter_config(COUNT_CYCLES, true);

    size_t result = 0;
    if (dpu_uncompressed_length(compressed, compressed_length, &result)) {
        return -1;
    }
    uncompressed_length = result;

    printf("Completed in %ld cycles\n", perfcounter_get());
    return 0;
}

