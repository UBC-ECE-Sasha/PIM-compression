# Snappy-C Compressor

This is an altered version of the C port of Google's Snappy compressor, ported by [Andi Kleen](http://github.com/andikleen/snappy-c). The alterations include:

* Removal of the decompression code, documentation generation, benchmark tool, random test and fuzzer test tools.
* Added argument to change the block input size that is used by Snappy.
* Changed format of compressed file to allow for multi-threaded decompression:
  * __Original Format:__
    ```
    <START FILE>
        <DECOMPRESSED LENGTH>
        <COMPRESSED DATA>
    <END FILE>
    ```
  * __Altered Format:__ compressed file consists of blocks that are individually compressed (rather than the whole file being compressed) and the compressed length of each block is pre-pended. Note that the compressed length includes the length taken up by storing the decompressed length plus the actual data itself.
   ```
    <START FILE>
        <SNAPPY BLOCK 1: COMPRESSED LENGTH>
            <DECOMPRESSED LENGTH>
            <COMPRESSED DATA>
        <SNAPPY BLOCK 2: COMPRESSED LENGTH>
            <DECOMPRESSED LENGTH>
            <COMPRESSED DATA>
        ...
    <END FILE>
    ```

## Usage
```./host_compress [-b <block size bytes>] <input file> [<output file>]```

If a block size is provided, the maximum decompressed length of each Snappy block is altered to use this value. The default block size is 32K (32768 bytes).
If no output file is provided, the output is saved to `<input file>.snp`.
