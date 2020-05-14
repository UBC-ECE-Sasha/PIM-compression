# PIM-compression
General-purpose compression algorithms in memory

__Test Files__
There are several test files in the 'test' directory. Each file is in uncompressed form, as well as compressed form using each of the implemented algorithms. The smallest files are good for manual debugging during the initial stages of implementation. The medium size files are for getting some confidence that corner cases are implemented correctly. The largest files are good for benchmarking performance under various scenarios and testing memory bounds in WRAM and MRAM.

* __alice__ (312 bytes) - some text from 'Alice In Wonderland'
* __coding__ (9423 bytes) - the Linux coding standard
* __terror2__ (105438 bytes) - some text from the 'Terrorists Handbook'
* __asyoulik__ (125179 bytes) - some text from Shakespeare's As You Like It
* __plarbn12__ (481861 bytes) - some poetry
* [missing] (~1MB) - 10x larger than the 100KB test for more data points in scalability measurements
* [missing] (~64MB) - to stress out the maximum size of MRAM for the DPU
  
## Snappy
Snappy is written by Google. It is meant for simple & fast decoding, generally for text files which are highly repetitive. The compression ratio can be quite good ('terror2' has a 2:1 ratio) considering the simplicity of the algorithm.
To encode files using the original Snappy format, the "scmd" tool found [here](http://github.com/andikless/snappy-c.git) can be used. The format used in our compressor/decompressor has been slightly modified to allow for a multi-threaded implementation. A description of this format can be found [here](https://github.com/UBC-ECE-Sasha/PIM-compression/tree/master/snappy/host-compress), alongside the compressor that was used to generate the test files.
