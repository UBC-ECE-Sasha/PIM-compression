#ifndef _SNAPPY_DECOMPRESSION_H_
#define _SNAPPY_DECOMPRESSION_H_

#include "dpu_snappy.h"

/**
 * Prepares the necessary constructs for running decompression.
 *
 * Traverses the input buffer to find the size of the decompressed file, decompressed
 * block size, and break down the input buffer into roughly equal sizes for each DPU
 * tasklet. Allocates the output buffer to match the size of the decompressed file.
 *
 * @param input: holds input buffer information
 * @param output: holds output buffer information
 * @param input_offset: holds starting input offset of each DPU tasklet
 * @param output_offset: holds starting output offset of each DPU tasklet
 * @return SNAPPY_OK if successful, error code otherwise
 */
snappy_status setup_decompression(struct host_buffer_context *input, struct host_buffer_context *output, uint32_t input_offset[NR_DPUS][NR_TASKLETS], uint32_t output_offset[NR_DPUS][NR_TASKLETS]);

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
 * @param input_offset: holds starting input offset of each DPU tasklet
 * @param output_offset: holds starting output offset of each DPU tasklet
 * @return SNAPPY_OK if successful, error code otherwise
 */
snappy_status snappy_decompress_dpu(struct host_buffer_context *input, struct host_buffer_context *output, uint32_t input_offset[NR_DPUS][NR_TASKLETS], uint32_t output_offset[NR_DPUS][NR_TASKLETS]);

#endif /* _SNAPPY_DECOMPRESSION_H_ */
