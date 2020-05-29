# PIM-compression/snappy

DPU implementation of Snappy decompression. 

Compression is done on the host CPU, decompression on DPU.

## Build

`make` to build both the host and DPU programs. 

The default number of DPUs used is 1 and the default number of DPU tasklets is 16. To override the default use:

`make NR_DPUS=<# dpus> NR_TASKLETS=<# tasks>`.

## Test

### Run all tests on host and DPU
```
make test
```

### Run specific test:
```
./decompress [-d] -i <snappy compressed file> [-o <output file>]
```

Use the `-d` option to run the DPU program. Otherwise the program is run on host.
If no output file is specified, the decompressed file is saved to `output.txt`, otherwise it is saved to the specified output.


