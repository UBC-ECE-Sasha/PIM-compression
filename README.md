# PIM-compression
General-purpose compression algorithms in memory.

__Test Files__

There are several test files in the 'test' directory. Each file is in uncompressed form, as well as compressed form using each of the implemented algorithms. The smallest files are good for manual debugging during the initial stages of implementation. The medium size files are for getting some confidence that corner cases are implemented correctly. The largest files are good for benchmarking performance under various scenarios and testing memory bounds in WRAM and MRAM.

* __alice__ (312 bytes) - some text from 'Alice In Wonderland'
* __coding__ (9423 bytes) - the Linux coding standard
* __terror2__ (105,438 bytes) - some text from the 'Terrorists Handbook'
* __plarbn12__ (481,861 bytes) - some poetry
* __world192__ (1,150,480 bytes) - some text from the CIA World Fact Book
* __xml__ (5,345,280 bytes) - collected XML files from Silesia Corpus
* __sao__ (7,251,945 bytes) - the SAO star catalog
* __dickens__ (10,192,446 bytes) - collected works of Charles Dickens
* __nci__ (33,553,445 bytes) - chemical database of structures
* __mozilla__ (51,220,480 bytes) - tarred executables of Mozilla 1.0
* __spamfile__ (84,217,482 bytes) - snapshot of collected spam emails
  
## Snappy
Snappy is written by Google. It is meant for simple & fast decoding, generally for text files which are highly repetitive. The compression ratio can be quite good ('terror2' has a 2:1 ratio) considering the simplicity of the algorithm.
To encode files using the original Snappy format, the "scmd" tool found [here](http://github.com/andikleen/snappy-c.git) can be used. The format used in our compressor/decompressor has been slightly modified to allow for a multi-threaded implementation. A description of this format can be found [here](https://github.com/UBC-ECE-Sasha/PIM-compression/tree/master/snappy/host-compress), alongside the compressor that was used to generate the test files.

## LZ4
LZ4 is a branch of the LZ77 compression algorithms, with a focus on compression and decompression speeds rather than compression ratio. The source code can be found [here](https://github.com/lz4/lz4)