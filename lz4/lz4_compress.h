#ifndef _LZ4_COMPRESSION_H_
#define _LZ4_COMPRESSION_H_

#include "dpu_lz4.h"

/**
 * Prepares the necessary constructs for compression.
 *
 * Allocates the output buffer to be the maximum expected size of
 * the compressed file.
 *
 * @param input: holds input buffer information
 * @param output: holds output buffer information
 * @param runtime: struct holding break down of runtimes for different parts of the program
 */
void setup_compression(struct host_buffer_context *input, struct host_buffer_context *output, struct program_runtime *runtime);

/**
 * Perform the LZ4 compression on the host.
 *
 * @param input: holds input buffer information
 * @param output: holds output buffer information
 * @param block_size: size to compress at a time
 * @return LZ4_OK if successful, error code otherwise
 */
lz4_status lz4_compress_host(struct host_buffer_context *input, struct host_buffer_context *output, uint32_t block_size);

/**
 * Perform the LZ4 compression on the DPU.
 *
 * @param in: holds the input buffer
 * @param in_len: length of input buffer
 * @param out: holds the output buffer
 * @param out_len: holds the output buffer length
 * @return LZ4_OK if successful, error code otherwise
 */
lz4_status lz4_compress_dpu(struct host_buffer_context *input, struct host_buffer_context *output, uint32_t block_size, struct program_runtime *runtime);


#endif /* _LZ4_COMPRESSION_H_ */
