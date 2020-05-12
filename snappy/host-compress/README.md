![snappy-c](http://halobates.de/snappy-c.png)

This is an altered version of the C port of Google's Snappy compressor, ported by [Andi Kleen](http://github.com/andikleen/snappy-c). The alterations include:

* Removal of the decompression code, documentation generation, benchmark tool, random test and fuzzer test tools.
* Added argument to change the block input size that is used by Snappy.
* Changed format of compressed file to allow for multi-threaded decompression:
  * Previous Format:
`[decompressed length][---------compressed data---------]`

  * New Format: compressed file consists of blocks that are individually compressed (rather than the whole file being compressed) and the compressed length of each block is pre-pended. The decompressed length has been replaced, as it is not as useful as the compressed length when parsing the compressed file.
  
`[compressed length][decompressed length][compressed data]  [compressed length][decompressed length][compressed data]...`
