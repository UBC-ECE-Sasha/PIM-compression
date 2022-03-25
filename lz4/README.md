# PIM-compression/lz4

DPU implementation of LZ4 compression and decompression. 

The implementation in this repository is highly based off of Yann Collet's [source code](https://github.com/lz4/lz4), and configured to work with 1 DPU and 1 Tasklet per block. There are two significant alterations:

* Block input size used by LZ4 is set to 4K, rather than the minimum 64KB size in the original code.
* LZ4 only handles one 4K block per invocation; the handling of larger blocks must be done external to this module.
  * __Original Format:__ compressed file consists of the decompressed length followed by the compressed data.
	```
	<START FILE>
		<DECOMPRESSED LENGTH (varint)>
		<COMPRESSED DATA (4K block)>
	<END FILE>
	```
## Build

`make` to build both the host and DPU programs. 

### Run specific test:
```
./dpu\_lz4 [-d] [-c] -i <input file> [-o <output file>]
```

* Use the `-d` option to run the DPU program. Otherwise the program is run on host.
* Use the `-c` option to perform compression on the input file. Otherwise, decompression is performed on the input file.
* If no output file is specified, the decompressed file is saved to `output.txt`, otherwise it is saved to the specified output.
