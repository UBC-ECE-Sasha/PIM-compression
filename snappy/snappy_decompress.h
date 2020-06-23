#ifndef _SNAPPY_DECOMPRESSION_H_
#define _SNAPPY_DECOMPRESSION_H_

#include "dpu_snappy.h"

/**
 * Prepares the necessary constructs for running decompression.
 * Allocates the output buffer to match the size of the decompressed file.
 *
 * @param input: holds input buffer information
 * @param output: holds output buffer information
 * @param preproc_time: time it takes to set up decompression, added to existing value
 * @return SNAPPY_OK if successful, error code otherwise
 */
snappy_status setup_decompression(struct host_buffer_context *input, struct host_buffer_context *output, double *preproc_time);

/**
 * Perform the Snappy decompression on the host.
 *
 * @param input: holds input buffer information
 * @param output: holds output buffer information
 * @return SNAPPY_OK if successful, error code otherwise
 */
snappy_status snappy_decompress_host(struct host_buffer_context *input, struct host_buffer_context *output);

/**
 * Perform the Snappy decompression on the DPU.
 *
 * @param input: holds input buffer information
 * @param output: holds output buffer information
 * @param preproc_time: time it takes to set up decompression and load the DPU, added to existing value
 * @param postproc_time: time it takes to grab output data from DPU, added to existing value
 * @return SNAPPY_OK if successful, error code otherwise
 */
snappy_status snappy_decompress_dpu(struct host_buffer_context *input, struct host_buffer_context *output, double *preproc_time, double *postproc_time);

#endif /* _SNAPPY_DECOMPRESSION_H_ */
