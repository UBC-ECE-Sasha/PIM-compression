#include <mram.h>

#include "dpu_decompress.h"

#define BUF_SIZE (1 << 10)

__host uint64_t compressed_length;
__host uint64_t uncompressed_length;
__host char compressed[BUF_SIZE];

int main() {
    size_t result = 0;
    if (dpu_uncompressed_length(compressed, compressed_length, &result)) {
        return -1;
    }
    uncompressed_length = result;
    return 0;
}

