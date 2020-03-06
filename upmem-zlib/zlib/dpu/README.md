# Zlib on DPU

Port of Zlib for a DPU-based solution.

## Contents

- `Makefile` compile and build executables
- `host_simple_example.c` contains a host harnest:
- `dpu_decompress.c` the dpu entry point for decompression
- `common` sources required for all dpu programs
- `decompress` sources required for decompression on the dpu

## How to build

- Make sure to build the zlib library following the appropriate instructions which are found in `../Makefile.in`
- run `make`
