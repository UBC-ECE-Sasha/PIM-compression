# Snappy-C Compressor

This is an altered version of the C port of Google's Snappy compressor, ported by [Andi Kleen](http://github.com/andikleen/snappy-c). The alterations include:

* Removal of the decompression code, documentation generation, benchmark tool, random test and fuzzer test tools.
* Added argument to change the block input size that is used by Snappy.
* Changed format of compressed file to allow for multi-threaded decompression:
  * __Original Format:__ compressed file consists of the decompressed length followed by the compressed data. During compression, the input file is broken into 64K chunks and each chunk is compressed separately. This results in independent compressed blocks in the output file. However, it is not possible to determine where each compressed block starts and ends, as there is no identifier for the start of a new block or indication for how long each compessed block is. 
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

## Usage
```./host_compress [-b <block size bytes>] <input file> [<output file>]```

If a block size is provided, the maximum decompressed length of each Snappy block is altered to use this value. The default block size is 32K (32768 bytes).
If no output file is provided, the output is saved to `<input file>.snp`.
