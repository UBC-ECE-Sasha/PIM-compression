#ifndef _SNAPPY_DECOMPRESSION_H_
#define _SNAPPY_DECOMPRESSION_H_

#include "dpu_snappy.h"

/**
 * Prepares the necessary constructs for running decompression.
 * Allocates the output buffer to match the size of the decompressed file.
 *
 * @param input: holds input buffer information
 * @param output: holds output buffer information
 * @param runtime: struct holding breakdown of runtimes for different parts of the program
 * @return SNAPPY_OK if successful, error code otherwise
 */
snappy_status setup_decompression(struct host_buffer_context *input, struct host_buffer_context *output, struct program_runtime *runtime);

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
 * @param runtime: struct holding breakdown of runtimes for different parts of the program
 * @return SNAPPY_OK if successful, error code otherwise
 */
snappy_status snappy_decompress_dpu(struct host_buffer_context *input, struct host_buffer_context *output, struct program_runtime *runtime);

#endif /* _SNAPPY_DECOMPRESSION_H_ */
