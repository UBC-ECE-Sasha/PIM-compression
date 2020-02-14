#include <dpu.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#define DPU_DECOMPRESS_PROGRAM "decompress.dpu"

int main(argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s <input> <decompressed>\n", argv[0]);
        return EXIT_FAILURE;
    }

    // TODO: implement this
    fprintf(stdout, "Not implemented yet");
    return EXIT_SUCCESS
}