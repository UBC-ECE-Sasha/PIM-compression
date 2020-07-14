#include <dpu.h>
#include <dpu_memory.h>
#include <dpu_log.h>
#include <stdint.h>
#include <stdio.h>

#include "snappy_compress.h"

#define DPU_COMPRESS_PROGRAM "dpu-compress/compress.dpu"
#define TOTAL_NR_TASKLETS (NR_DPUS * NR_TASKLETS)

/**
 * This value could be halfed or quartered to save memory
 * at the cost of slightly worse compression.
 */
#define MAX_HASH_TABLE_BITS 14
#define MAX_HASH_TABLE_SIZE (1U << MAX_HASH_TABLE_BITS)

/**
 * Calculate the rounded down log base 2 of an unsigned integer.
 *
 * @param n: value to perform the calculation on
 * @return Log base 2 floor of n
 */
static inline int32_t log2_floor(uint32_t n)
{
	return (n == 0) ? -1 : 31 ^ __builtin_clz(n);
}

/**
 * Calculate the maximum expected compressed length for a given
 * uncompressed length.
 *	 
 * Compressed data can be defined as:
 *	  compressed := item* literal*
 *	  item		 := literal* copy
 *
 * The trailing literal sequence has a space blowup of at most 62/60
 * since a literal of length 60 needs one tag byte + one extra byte
 * for length information.
 *
 * Item blowup is trickier to measure.	Suppose the "copy" op copies
 * 4 bytes of data.  Because of a special check in the encoding code,
 * we produce a 4-byte copy only if the offset is < 65536.	Therefore
 * the copy op takes 3 bytes to encode, and this type of item leads
 * to at most the 62/60 blowup for representing literals.
 *
 * Suppose the "copy" op copies 5 bytes of data.  If the offset is big
 * enough, it will take 5 bytes to encode the copy op.	Therefore the
 * worst case here is a one-byte literal followed by a five-byte copy.
 * I.e., 6 bytes of input turn into 7 bytes of "compressed" data.
 *
 * This last factor dominates the blowup, so the final estimate is:
 */
static inline uint32_t snappy_max_compressed_length(uint32_t input_length) {
	if (input_length > 0) 
		return (32 + input_length + input_length / 6);
	else
		return 0;
}

/**
 * Write a varint to the output buffer. See the decompression code
 * for a description of this format.
 *
 * @param output: holds output buffer information
 * @param val: value to write
 */
static inline void write_varint32(struct host_buffer_context *output, uint32_t val)
{
	static const int mask = 128;

	if (val < (1 << 7)) {
		*(output->curr++) = val;
	}
	else if (val < (1 << 14)) {
		*(output->curr++) = val | mask;
		*(output->curr++) = val >> 7;
	}
	else if (val < (1 << 21)) {
		*(output->curr++) = val | mask;
		*(output->curr++) = (val >> 7) | mask;
		*(output->curr++) = val >> 14;
	}
	else if (val < (1 << 28)) {
		*(output->curr++) = val | mask;
		*(output->curr++) = (val >> 7) | mask;
		*(output->curr++) = (val >> 14) | mask;
		*(output->curr++) = val >> 21;
	}
	else {
		*(output->curr++) = val | mask;
		*(output->curr++) = (val >> 7) | mask;
		*(output->curr++) = (val >> 14) | mask;
		*(output->curr++) = (val >> 21) | mask;
		*(output->curr++) = val >> 28;
	}
}

/**
 * Write an unsigned integer to the output buffer.
 *
 * @param ptr: pointer where to write the integer
 * @param val: value to write
 */
static inline void write_uint32(uint8_t *ptr, uint32_t val)
{
	*(ptr++) = val & 0xFF;
	*(ptr++) = (val >> 8) & 0xFF;
	*(ptr++) = (val >> 16) & 0xFF;
	*(ptr++) = (val >> 24) & 0xFF;
}

/**
 * Read an unsigned integer from the input buffer.
 *
 * @param ptr: where to read the integer from
 * @return Value read
 */
static inline uint32_t read_uint32(uint8_t *ptr)
{
	uint32_t val = 0;
	
	val |= *ptr++ & 0xFF;
	val |= (*ptr++ & 0xFF) << 8;
	val |= (*ptr++ & 0xFF) << 16;
	val |= (*ptr++ & 0xFF) << 24;
	return val;
}

/**
 * Get the size of the hash table needed for the size we are
 * compressing, and reset the values in the table.
 *
 * @param table: pointer to the start of the hash table
 * @param size_to_compress: size we are compressing
 * @param table_size[out]: size of the table needed to compress size_to_compress
 */
static inline void get_hash_table(uint16_t *table, uint32_t size_to_compress, uint32_t *table_size)
{
	*table_size = 256;
	while ((*table_size < MAX_HASH_TABLE_SIZE) && (*table_size < size_to_compress))
		*table_size <<= 1;

	memset(table, 0, *table_size * sizeof(*table));
}

/**
 * Hash function.
 *
 * Any hash function will produce a valid compressed bitstream, but a good
 * hash function reduces the number of collisions and thus yields better
 * compression for compressible input, and more speed for incompressible
 * input. Of course, it doesn't hurt if the hash function is reasonably fast
 * either, as it gets called a lot.
 *
 * @param ptr: pointer to the value we want to hash
 * @param shift: adjusts hash to be within table size
 * @return Hash of four bytes stored at ptr
 */
static inline uint32_t hash(uint8_t *ptr, int shift)
{
	uint32_t kmul = 0x1e35a7bd;
	uint32_t bytes = read_uint32(ptr);
	return (bytes * kmul) >> shift;
}

/**
 * Find the number of bytes in common between s1 and s2.
 *
 * @param s1: first buffer to compare
 * @param s2: second buffer to compare
 * @param s2_limit: end of second buffer to compare
 * @return Number of bytes in common between s1 and s2
 */
static inline int32_t find_match_length(uint8_t *s1, uint8_t *s2, uint8_t *s2_limit)
{
	int32_t matched = 0;
	
	// Check by increments of 4 first
	while ((s2 <= (s2_limit - 4)) && (read_uint32(s2) == read_uint32(s1 + matched))) {
		s2 += 4;
		matched += 4;
	}

	// Remaining bytes
	while ((s2 < s2_limit) && (s1[matched] == *s2)) {
		s2++;
		matched++;
	}
	
	return matched;
}

/**
 * Emit a literal element.
 *
 * @param output: holds output buffer information
 * @param literal: buffer storing the literal data
 * @param len: length of the literal
 */
static void emit_literal(struct host_buffer_context *output, uint8_t *literal, uint32_t len)
{
	//printf("emit_literal %d %d\n", len, output->curr-output->buffer);
	uint32_t n = len - 1; // Zero-length literals are disallowed
	
	if (n < 60) {
		*output->curr++ = EL_TYPE_LITERAL | (n << 2);
	}
	else {
		uint8_t *base = output->curr;
		uint8_t count = 0;
		output->curr++;
		while (n > 0) {
			*output->curr++ = n & 0xFF;
			n >>= 8;
			count++;
		}

		*base = EL_TYPE_LITERAL | ((59 + count) << 2);
	}
	
	memcpy(output->curr, literal, len);
	output->curr += len;
}

/**
 * Emit a copy element that is less than 64-bytes in length.
 *
 * @param output: holds output buffer information
 * @param offset: offset of the copy
 * @param len: length of the copy
 */
static void emit_copy_less_than64(struct host_buffer_context *output, uint32_t offset, uint32_t len)
{
	if ((len < 12) && (offset < 2048)) {
		*output->curr++ = EL_TYPE_COPY_1 + ((len - 4) << 2) + ((offset >> 8) << 5);
		*output->curr++ = offset & 0xFF;
	}
	else {
		*output->curr++ = EL_TYPE_COPY_2 + ((len - 1) << 2);
		*output->curr++ = offset & 0xFF;
		*output->curr++ = (offset >> 8) & 0xFF;
	}
}

/**
 * Emit copy elements in chunks of length 64-bytes.
 *
 * @param output: holds output buffer information
 * @param offset: offset of the copy
 * @param len: length of the copy
 */
static void emit_copy(struct host_buffer_context *output, uint32_t offset, uint32_t len) 
{
	//printf("emit_copy %d %d %d\n", offset, len, output->curr - output->buffer);
	
	// Emit 64-byte copies but keep at least four bytes reserved
	while (len >= 68) {
		emit_copy_less_than64(output, offset, 64);
		len -= 64;
	}

	// Emit an extra 60-byte copy if we have too much data to fit in one copy
	if (len > 64) {
		emit_copy_less_than64(output, offset, 60);
		len -= 60;
	}

	// Emit remainder
	emit_copy_less_than64(output, offset, len);
}

/**
 * Perform Snappy compression on a block of input data, and save the compressed
 * data to the output buffer.
 *
 * @param input: holds input buffer information
 * @param output: holds output buffer information
 * @param input_size: size of the input to compress
 * @param table: pointer to allocated hash table
 * @param table_size: size of the hash table
 */
static void compress_block(struct host_buffer_context *input, struct host_buffer_context *output, uint32_t input_size, uint16_t *table, uint32_t table_size)
{
	uint8_t *base_input = input->curr;
	uint8_t *input_end = input->curr + input_size;
	const int32_t shift = 32 - log2_floor(table_size);

	// Make space for compressed length
	output->curr += 4;
	uint8_t *output_start = output->curr;

	/*
	 * Bytes in [next_emit, input->curr) will be emitted as literal bytes.
	 * Or [next_emit, input_end) after the main loop.
	 */
	uint8_t *next_emit = input->curr;
	const uint32_t input_margin_bytes = 15;

	if (input_size >= input_margin_bytes) {
		const uint8_t *const input_limit = input->curr + input_size - input_margin_bytes;
		
		uint32_t next_hash;
		for (next_hash = hash(++input->curr, shift);;) {
			/*
			 * The body of this loop calls EmitLiteral once and then EmitCopy one or
			 * more times.	(The exception is that when we're close to exhausting
			 * the input we goto emit_remainder.)
			 *
			 * In the first iteration of this loop we're just starting, so
			 * there's nothing to copy, so calling EmitLiteral once is
			 * necessary.  And we only start a new iteration when the
			 * current iteration has determined that a call to EmitLiteral will
			 * precede the next call to EmitCopy (if any).
			 *
			 * Step 1: Scan forward in the input looking for a 4-byte-long match.
			 * If we get close to exhausting the input then goto emit_remainder.
			 *
			 * Heuristic match skipping: If 32 bytes are scanned with no matches
			 * found, start looking only at every other byte. If 32 more bytes are
			 * scanned, look at every third byte, etc.. When a match is found,
			 * immediately go back to looking at every byte. This is a small loss
			 * (~5% performance, ~0.1% density) for lcompressible data due to more
			 * bookkeeping, but for non-compressible data (such as JPEG) it's a huge
			 * win since the compressor quickly "realizes" the data is incompressible
			 * and doesn't bother looking for matches everywhere.
			 *
			 * The "skip" variable keeps track of how many bytes there are since the
			 * last match; dividing it by 32 (ie. right-shifting by five) gives the
			 * number of bytes to move ahead for each iteration.
			 */
			uint32_t skip_bytes = 32;
			uint8_t *next_input = input->curr;
			uint8_t *candidate;
			do {
				input->curr = next_input;
				uint32_t hval = next_hash;
				uint32_t bytes_between_hash_lookups = skip_bytes++ >> 5;
				next_input = input->curr + bytes_between_hash_lookups;

				if (next_input > input_limit)
					goto emit_remainder;

				next_hash = hash(next_input, shift);
				candidate = base_input + table[hval];
				table[hval] = input->curr - base_input;
			} while (read_uint32(input->curr) != read_uint32(candidate));
			
			/*
			 * Step 2: A 4-byte match has been found.  We'll later see if more
			 * than 4 bytes match.	But, prior to the match, input bytes
			 * [next_emit, input->curr) are unmatched.	Emit them as "literal bytes."
			 */
			emit_literal(output, next_emit, input->curr - next_emit);

			/*
			 * Step 3: Call EmitCopy, and then see if another EmitCopy could
			 * be our next move.  Repeat until we find no match for the
			 * input immediately after what was consumed by the last EmitCopy call.
			 *
			 * If we exit this loop normally then we need to call EmitLiteral next,
			 * though we don't yet know how big the literal will be.  We handle that
			 * by proceeding to the next iteration of the main loop.  We also can exit
			 * this loop via goto if we get close to exhausting the input.
			 */
			uint8_t *insert_tail;
			uint32_t candidate_bytes = 0;

			do {
				/*
				 * We have a 4-byte match at input->curr, and no need to emit any
				 *	"literal bytes" prior to input->curr.
				 */
				const uint8_t *base = input->curr;
				int32_t matched = 4 + find_match_length(candidate + 4, input->curr + 4, input_end);
				input->curr += matched;

				int32_t offset = base - candidate;
				emit_copy(output, offset, matched);
			
				/*
				 * We could immediately start working at input->curr now, but to improve
				 * compression we first update table[Hash(input->curr - 1, ...)]/
				 */
				insert_tail = input->curr - 1;
				next_emit = input->curr;
				if (input->curr >= input_limit)
					goto emit_remainder;

				uint32_t prev_hash = hash(insert_tail, shift);
				table[prev_hash] = input->curr - base_input - 1;

				uint32_t curr_hash = hash(insert_tail + 1, shift);
				candidate = base_input + table[curr_hash];
				candidate_bytes = read_uint32(candidate);
				table[curr_hash] = input->curr - base_input;
			} while(read_uint32(insert_tail + 1) == candidate_bytes);

			next_hash = hash(insert_tail + 2, shift);
			input->curr++;
		}
	}
				
emit_remainder:
	/* Emit the remaining bytes as literal */
	if (next_emit < input_end) {
		emit_literal(output, next_emit, input_end - next_emit);
		input->curr = input_end;
	}

	write_uint32(output_start - 4, output->curr - output_start);
}


/*************** Public Functions *******************/

void setup_compression(struct host_buffer_context *input, struct host_buffer_context *output, double *preproc_time) 
{
	struct timeval start;
	struct timeval end;
	gettimeofday(&start, NULL);

	/*
	 * Compressed data can be defined as:
	 *	  compressed := item* literal*
	 *	  item		 := literal* copy
	 *
	 * The trailing literal sequence has a space blowup of at most 62/60
	 * since a literal of length 60 needs one tag byte + one extra byte
	 * for length information.
	 *
	 * Item blowup is trickier to measure.	Suppose the "copy" op copies
	 * 4 bytes of data.  Because of a special check in the encoding code,
	 * we produce a 4-byte copy only if the offset is < 65536.	Therefore
	 * the copy op takes 3 bytes to encode, and this type of item leads
	 * to at most the 62/60 blowup for representing literals.
	 *
	 * Suppose the "copy" op copies 5 bytes of data.  If the offset is big
	 * enough, it will take 5 bytes to encode the copy op.	Therefore the
	 * worst case here is a one-byte literal followed by a five-byte copy.
	 * I.e., 6 bytes of input turn into 7 bytes of "compressed" data.
	 *
	 * This last factor dominates the blowup, so the final estimate is:
	 */
	uint32_t max_compressed_length = snappy_max_compressed_length(input->length);
	output->buffer = malloc(sizeof(uint8_t) * max_compressed_length);
	output->curr = output->buffer;
	output->length = 0;

	gettimeofday(&end, NULL);
	*preproc_time += get_runtime(&start, &end);
}

snappy_status snappy_compress_host(struct host_buffer_context *input, struct host_buffer_context *output, uint32_t block_size)
{
	// Allocate the hash table for compression
	uint16_t *table = malloc(sizeof(uint16_t) * MAX_HASH_TABLE_SIZE);

	// Write the decompressed length
	uint32_t length_remain = input->length;
	write_varint32(output, length_remain);

	// Write the decompressed block size
	write_varint32(output, block_size);

	while (input->curr < (input->buffer + input->length)) {
		// Get the next block size ot compress
		uint32_t to_compress = MIN(length_remain, block_size);

		// Get the size of the hash table used for this block
		uint32_t table_size;
		get_hash_table(table, to_compress, &table_size);
		
		// Compress the current block
		compress_block(input, output, to_compress, table, table_size);
		
		length_remain -= to_compress;
	}

	// Update output length
	output->length = (output->curr - output->buffer);

	return SNAPPY_OK;
}

snappy_status snappy_compress_dpu(struct host_buffer_context *input, struct host_buffer_context *output, uint32_t block_size, double *preproc_time, double *postproc_time)
{
	struct timeval start;
	struct timeval end;
	gettimeofday(&start, NULL);

	// Calculate the workload of each task
	uint32_t num_blocks = (input->length + block_size - 1) / block_size;
	uint32_t input_blocks_per_dpu = (num_blocks + NR_DPUS - 1) / NR_DPUS;
	uint32_t input_blocks_per_task = (num_blocks + TOTAL_NR_TASKLETS - 1) / TOTAL_NR_TASKLETS;

	uint32_t input_block_offset[NR_DPUS][NR_TASKLETS] = {0};
	uint32_t output_offset[NR_DPUS][NR_TASKLETS] = {0};
	
	uint32_t dpu_idx = 0;
	uint32_t task_idx = 0;
	uint32_t dpu_blocks = 0;
	for (uint32_t i = 0; i < num_blocks; i++) {
		// If we have reached the next DPU's boundary, update the index
		if (dpu_blocks == input_blocks_per_dpu) {
			dpu_idx++;
			task_idx = 0;
			dpu_blocks = 0;
		}
		
		// If we have reached the next tasks's boundary, log the offset
		if (dpu_blocks == (input_blocks_per_task * task_idx)) {
			input_block_offset[dpu_idx][task_idx] = i;
			output_offset[dpu_idx][task_idx] = ALIGN(snappy_max_compressed_length(block_size * dpu_blocks), 64);
			task_idx++;
		}

		dpu_blocks++;
	}

	// Write the decompressed block size and length
	write_varint32(output, input->length);
	write_varint32(output, block_size);
	output->length = output->curr - output->buffer;

	// Allocate DPUs
	struct dpu_set_t dpus;
	struct dpu_set_t dpu;
	DPU_ASSERT(dpu_alloc(NR_DPUS, NULL, &dpus));

	dpu_idx = 0;
	uint32_t input_buffer_start = 1024 * 1024;
	uint32_t output_buffer_start[NR_DPUS];
	DPU_FOREACH(dpus, dpu) {
		// Add check to get rid of array out of bounds compiler warning
		if (dpu_idx == NR_DPUS)
			break; 

		uint32_t input_length = 0;
		if ((dpu_idx != (NR_DPUS - 1)) && (input_block_offset[dpu_idx + 1][0] != 0)) {
			uint32_t blocks = (input_block_offset[dpu_idx + 1][0] - input_block_offset[dpu_idx][0]);
			input_length = blocks * block_size;
		}
		else if ((dpu_idx == 0) || (input_block_offset[dpu_idx][0] != 0)) {
			input_length = input->length - (input_block_offset[dpu_idx][0] * block_size);
		} 
		
		output_buffer_start[dpu_idx] = ALIGN(input_buffer_start + input_length, 64);

		// Set up and load the DPU program
		DPU_ASSERT(dpu_load(dpu, DPU_COMPRESS_PROGRAM, NULL));
		DPU_ASSERT(dpu_copy_to(dpu, "block_size", 0, &block_size, sizeof(uint32_t)));
		DPU_ASSERT(dpu_copy_to(dpu, "input_length", 0, &input_length, sizeof(uint32_t)));
		DPU_ASSERT(dpu_copy_to(dpu, "input_block_offset", 0, input_block_offset[dpu_idx], sizeof(uint32_t) * NR_TASKLETS));
		DPU_ASSERT(dpu_copy_to(dpu, "output_offset", 0, output_offset[dpu_idx], sizeof(uint32_t) * NR_TASKLETS));
		DPU_ASSERT(dpu_copy_to(dpu, "input_buffer", 0, &input_buffer_start, sizeof(uint32_t)));
		DPU_ASSERT(dpu_copy_to(dpu, "output_buffer", 0, &output_buffer_start[dpu_idx], sizeof(uint32_t)));
		DPU_ASSERT(dpu_copy_to_mram(dpu.dpu, input_buffer_start, input->curr + (input_block_offset[dpu_idx][0] * block_size), ALIGN(input_length, 8)));

		dpu_idx++;
	}

	gettimeofday(&end, NULL);
	*preproc_time += get_runtime(&start, &end);
	
	// Launch all DPUs
	int ret = dpu_launch(dpus, DPU_SYNCHRONOUS);
	if (ret != 0)
	{
		DPU_ASSERT(dpu_free(dpus));
		return SNAPPY_INVALID_INPUT;
	}

	gettimeofday(&start, NULL);

	// Deallocate the DPUs
	dpu_idx = 0;
	DPU_FOREACH(dpus, dpu) {
		// Get the output length of each tasklet
		uint32_t output_length[NR_TASKLETS];
		DPU_ASSERT(dpu_copy_from(dpu, "output_length", 0, output_length, sizeof(uint32_t) * NR_TASKLETS));

		for (uint8_t i = 0; i < NR_TASKLETS; i++) {
			if (output_length[i] != 0) {
				// Read the data from the current tasklet
				DPU_ASSERT(dpu_copy_from_mram(dpu.dpu, output->curr, output_buffer_start[dpu_idx] + output_offset[dpu_idx][i], ALIGN(output_length[i], 8)));
				
				output->length += output_length[i];
				output->curr += output_length[i];
			}			
		}
		
		printf("------DPU %d Logs------\n", dpu_idx);
		DPU_ASSERT(dpu_log_read(dpu, stdout));
		dpu_idx++;
	}

	DPU_ASSERT(dpu_free(dpus));

	gettimeofday(&end, NULL);
	*postproc_time += get_runtime(&start, &end);

	return SNAPPY_OK;
}
