# PIM-compression/snappy

DPU implementation of Snappy compression and decompression. 

The implementation in this repository is highly based off of the C port of Google's Snappy compressor, ported by [Andi Kleen](http://github.com/andikleen/snappy-c), with two important alterations:

* Block input size used by Snappy is now configurable (up to 64KB), rather than the set 64KB size in the original code. 
* Format of compressed file is changed to allow for multi-threaded decompression:
  * __Original Format:__ compressed file consists of the decompressed length followed by the compressed data. During compression, the input file is broken into 64KB chunks and each chunk is compressed separately. This results in independent compressed blocks in the output file. However, it is not possible to determine where each compressed block starts and ends, as there is no identifier for the start of a new block or indication for how long each compessed block is.
	```
	<START FILE>
		<DECOMPRESSED LENGTH (varint)>
		<COMPRESSED DATA>
			<BLOCK 1>
			<BLOCK 2>
			...
	<END FILE>
	```
  * __Altered Format:__ start of the compressed file is provided with more information to allow for determining where each compressed block is. This allows for a multi-threaded solution, because the compressed file can be broken up into independent pieces and parsed separately.
	```
	<START FILE>
		<DECOMPRESSED LENGTH (varint)>
		<DECOMPRESSED BLOCK SIZE (varint)>
		<COMPRESSED LENGTHS>
			<BLOCK 1 (int)>
			<BLOCK 2 (int)>
			...
		<COMPRESSED DATA>
			<BLOCK 1>
			<BLOCK 2>
			...
	<END FILE>
	```

## Build

`make` to build both the host and DPU programs. 

The default number of DPUs used is 1 and the default number of DPU tasklets is 1. To override the default use:

`make NR_DPUS=<# dpus> NR_TASKLETS=<# tasks>`.

## Test

### Run all decompression tests on host and DPU
```
make test
```

### Run all decompression tests on only host or DPU
```
make test_host
```

```
make test_dpu
```

### Run specific test:
```
./dpu\_snappy [-d] [-c] [-b <block_size>] -i <input file> [-o <output file>]
```

* Use the `-d` option to run the DPU program. Otherwise the program is run on host.
* Use the `-c` option to perform compression on the input file. Otherwise, decompression is performed on the input file.
* Use the `-b` option to specify a block size for use during compression, default is 32KB.
* If no output file is specified, the decompressed file is saved to `output.txt`, otherwise it is saved to the specified output.
