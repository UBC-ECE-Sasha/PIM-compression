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
 */
void setup_compression(struct host_buffer_context *input, struct host_buffer_context *output);

/**
 * Perform the Snappy compression on the host.
 *
 * @param input: holds input buffer information
 * @param output: holds output buffer information
 * @return SNAPPY_OK if successful, error code otherwise
 */
snappy_status snappy_compress_host(struct host_buffer_context *input, struct host_buffer_context *output, uint32_t block_size);

#endif /* _SNAPPY_COMPRESSION_H_ */
