#ifndef _LZ4_DECOMPRESSION_H_
#define _LZ4_DECOMPRESSION_H_

#include "dpu_lz4.h"

/**
 * Prepares the necessary constructs for running decompression.
 * Allocates the output buffer to match the size of the decompressed file.
 *
 * @param input: holds input buffer information
 * @param output: holds output buffer information
 * @param runtime: struct holding breakdown of runtimes for different parts of the program
 * @return LZ4_OK if successful, error code otherwise
 */
lz4_status setup_decompression(struct host_buffer_context *input, struct host_buffer_context *output, struct program_runtime *runtime);

/**
 * Perform the LZ4 decompression on the host.
 *
 * @param input: holds input buffer information
 * @param output: holds output buffer information
 * @return LZ4_OK if successful, error code otherwise
 */
lz4_status lz4_decompress_host(struct host_buffer_context *input, struct host_buffer_context *output);

/**
 * Perform the LZ4 decompression on the DPU.
 *
 * @param in: holds the input buffer
 * @param in_len: length of input buffer
 * @param out: holds the output buffer
 * @param out_len: holds the output buffer length
 * @return LZ4_OK if successful, error code otherwise
 */
lz4_status lz4_decompress_dpu(unsigned char *in, size_t in_len, unsigned char *out, size_t *out_len);

#endif /* _LZ4_DECOMPRESSION_H_ */
