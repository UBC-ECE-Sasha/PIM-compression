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
 * @param preproc_time: time it takes to setup compression, added to existing value
 */
void setup_compression(struct host_buffer_context *input, struct host_buffer_context *output, double *preproc_time);

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
 * @param preproc_time: time it takes to set up compression and load the DPU, added to existing value
 * @param postproc_time: time it takes to grab outptu data from DPU, added to existing value
 * @return SNAPPY_OK if successful, error code otherwise
 */
snappy_status snappy_compress_dpu(struct host_buffer_context *input, struct host_buffer_context *output, uint32_t block_size, double *preproc_time, double *postproc_time);


#endif /* _SNAPPY_COMPRESSION_H_ */
