# PIM-compression/snappy

DPU implementation of snappy decompression. 

Compression is done on the host CPU, decompression on DPU.

## Build

`make` to build both the host and DPU programs.

## Test

```
./decompress_host test/alice.snappy
```

This should return the uncompressed size of the test file (895).

`alice.snappy` is text file that was originally compressed with snappy.

### Initial result

48 cycles to calculate size of test/alice.snappy
48 instructions

