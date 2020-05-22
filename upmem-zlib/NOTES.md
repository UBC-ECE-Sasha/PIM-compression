# Notes

Summary of what has been done.

* Worth noting from the RFC that the spec "does not attempt to allow random access to compressed data"
* Took the Zlib library and tried to adapt it to the DPU
* Was able to compile the library for the DPU  by copying and including missing header files which are in the `common` directory
* We used the `-O2 -flto` optimization compilation flags early on because the `.text` region wouldn't fit in IRAM
* Trimmed down various parts of inflate:
  * Removed the `CRC32` code, we only need to use the `Adler` checksum code
  * Removed several functions `inflate.c` that seemed to be only used for testing/verification
* Ran into an issue with clang compiler not able to lower memory intrinsic, fixed this by (mostly) using `fno-builtin-memcpy` compilation flag and by doing manual copies whenever we see a `memcpy` 
* Prefixed a lot of the regular pointers in `struct inflate_state` and `struct z_stream_s` with `__mram_ptr`
* In `zutil.c`, we modified `zcalloc` and `zfree` functions to return MRAM pointers (manual heap memory management)
* There's a function called `mram_memcpy` floating around in `inflate.c` that is probably not documented anywhere. It does a regular `memcpy` but for MRAM pointers

