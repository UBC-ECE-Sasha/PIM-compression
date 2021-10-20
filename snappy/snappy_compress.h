#ifndef _SNAPPY_COMPRESSION_H_
#define _SNAPPY_COMPRESSION_H_

#include "dpu_snappy.h"

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
 * Perform the Snappy compression on the host.
 *
 * @param input: holds input buffer information
 * @param output: holds output buffer information
 * @param block_size: size to compress at a time
 * @return SNAPPY_OK if successful, error code otherwise
 */
snappy_status snappy_compress_host(struct host_buffer_context *input, struct host_buffer_context *output, uint32_t block_size);

/**
 * Perform the Snappy compression on the DPU.
 *
 * @param input: holds input buffer information
 * @param output: holds output buffer information
 * @param block_size: size to compress at a time
 * @param runtime: struct holding break down of runtimes for different parts of the program
 * @return SNAPPY_OK if successful, error code otherwise
 */
snappy_status snappy_compress_dpu(const unsigned char *in, size_t in_len, unsigned char *out, size_t *out_len,
            void *wrkmem, struct program_runtime *runtime);


#endif /* _SNAPPY_COMPRESSION_H_ */
