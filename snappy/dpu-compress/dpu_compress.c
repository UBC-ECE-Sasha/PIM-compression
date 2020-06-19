#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <mram.h>
#include <defs.h>
#include "alloc.h"
#include "built_ins.h"

#include "dpu_compress.h"

/**
 * This value could be halfed or quartered to save memory
 * at the cost of slightly worse compression.
 */
#define MAX_HASH_TABLE_BITS 10
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
 * Advance the sequential reader by some amount.
 *
 * @param input: holds input buffer information
 * @param len: number of bytes to advance seqential reader by
 */
static inline void advance_seqread(struct in_buffer_context *input, uint32_t len)
{
	__mram_ptr uint8_t *curr_ptr = seqread_tell(input->ptr, &input->sr);
	input->ptr = seqread_seek(curr_ptr + len, &input->sr);
}

/**
 * Read an unsigned integer from input_buffer in MRAM.
 *
 * @param input: holds input buffer information
 * @param offset: offset from start of input_buffer to read from
 * @return Value read
 */
static inline uint32_t read_uint32(struct in_buffer_context *input, uint32_t offset)
{
	uint8_t data_read[16];
	mram_read(&input->buffer[WINDOW_ALIGN(offset, 8)], data_read, 16);

	offset %= 8;
	return (data_read[offset] |
			(data_read[offset + 1] << 8) |
			(data_read[offset + 2] << 16) |
			(data_read[offset + 3] << 24)); 
}

/**
 * Write data to the output buffer. If append buffer becomes full, it is written
 * to MRAM and a new buffer is started.
 *
 * @param output: holds output buffer information
 * @param arr: buffer holding data to write
 * @param len: length of data to write
 */
static void write_output_buffer(struct out_buffer_context *output, uint8_t *arr, uint32_t len)
{
	uint32_t curr_index = output->curr - output->append_window;
	while (len) {
		uint32_t to_write = MIN(OUT_BUFFER_LENGTH - curr_index, len);
		memcpy(&output->append_ptr[curr_index], arr, to_write);

		len -= to_write;
		curr_index += to_write;
		output->curr += to_write;
		arr += to_write;

		// If we are past the append window, write out current window to MRAM and start a new one
		if (curr_index >= OUT_BUFFER_LENGTH) {
			dbg_printf("Past EOB - writing back output %d\n", output->append_window);

			mram_write(output->append_ptr, &output->buffer[output->append_window], OUT_BUFFER_LENGTH);
			output->append_window += OUT_BUFFER_LENGTH;
			curr_index = 0;
		}
	}
}

/**
 * Copy data from the current location in the input buffer to the output buffer. 
 * Manages the append window in the same way as the previous function.
 *
 * @param input: holds input buffer information
 * @param output: holds output buffer information
 * @param len: length of data to copy
 */
static void copy_output_buffer(struct in_buffer_context *input, struct out_buffer_context *output, uint32_t len)
{
	uint32_t curr_index = output->curr - output->append_window;
	while (len) {
		uint32_t to_copy = MIN(OUT_BUFFER_LENGTH - curr_index, len);
		memcpy(&output->append_ptr[curr_index], input->ptr, to_copy);

		// Advance sequential reader
		advance_seqread(input, to_copy);
		
		len -= to_copy;
		curr_index += to_copy;
		output->curr += to_copy;

		// If we are past the append window, write out current window to MRAM and start a new one
		if (curr_index >= OUT_BUFFER_LENGTH) {
			dbg_printf("Past EOB - writing back output %d\n", output->append_window);

			mram_write(output->append_ptr, &output->buffer[output->append_window], OUT_BUFFER_LENGTH);
			output->append_window += OUT_BUFFER_LENGTH;
			curr_index = 0;
		}
	}
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
 * @param input: holds input buffer information
 * @param ptr: pointer to the value we want to hash
 * @param shift: adjusts hash to be within table size
 * @return Hash of four bytes stored at ptr
 */
static inline uint32_t hash(struct in_buffer_context *input, uint32_t ptr, int shift)
{
/*	uint32_t kmul = 0x1e35a7bd;
	uint32_t bytes = read_uint32(ptr);
	return (bytes * kmul) >> shift; */
	uint32_t hash1, hash2;
	uint32_t bytes = read_uint32(input, ptr);
	__builtin_hash_rrr(hash1, bytes, 0xFFFFF);
	__builtin_hash_rrr(hash2, bytes >> 2, 0xFFFFF);
	return hash1 ^ hash2; 
}

/**
 * Find the number of bytes in common between s1 and s2.
 *
 * @param input: holds input buffer information
 * @param s1: offset of first buffer from input buffer start
 * @param s2: offset of second buffer from input buffer start
 * @param s2_limit: offset of end of second bufer from input buffer start
 * @return Number of bytes in common between s1 and s2
 */
static inline int32_t find_match_length(struct in_buffer_context *input, uint32_t s1, uint32_t s2, uint32_t s2_limit)
{
	int32_t matched = 0;
	
	// Check by increments of 4 first
	while ((s2 <= (s2_limit - 4)) && (read_uint32(input, s2) == read_uint32(input, s1 + matched))) {
		s2 += 4;
		matched += 4;
	}

	// Remaining bytes
	uint32_t x = read_uint32(input, s1 + matched) ^ read_uint32(input, s2);
	matched += MIN((__builtin_ctz(x) >> 3), s2_limit - s2);

	return matched;
}

/**
 * Emit a literal element from the current location in the input buffer.
 *
 * @param input: holds input buffer information
 * @param output: holds output buffer information
 * @param len: length of the literal
 */
static void emit_literal(struct in_buffer_context *input, struct out_buffer_context *output, uint32_t len)
{
	//printf("emit_literal %d %d\n", len, output->curr);
	uint8_t tag[5];
	uint8_t tag_len = 0;

	uint32_t n = len - 1; // Zero-length literals are disallowed
	if (n < 60) {
		tag[0] = EL_TYPE_LITERAL | (n << 2);
		tag_len = 1;
	}
	else {
		uint8_t count = 1;
		while (n > 0) {
			tag[count] = n & 0xFF;
			n >>= 8;
			count++;
		}

		tag[0] = EL_TYPE_LITERAL | ((58 + count) << 2);
		tag_len = count;
	}

	write_output_buffer(output, tag, tag_len);
	copy_output_buffer(input, output, len);
}

/**
 * Emit a copy element that is less than 64-bytes in length.
 *
 * @param output: holds output buffer information
 * @param offset: offset of the copy
 * @param len: length of the copy
 */
static void emit_copy_less_than64(struct out_buffer_context *output, uint32_t offset, uint32_t len)
{
	uint8_t tag[3];
	uint8_t tag_len = 0;

	if ((len < 12) && (offset < 2048)) {
		tag[0] = EL_TYPE_COPY_1 + ((len - 4) << 2) + ((offset >> 8) << 5);
		tag[1] = offset & 0xFF;
		tag_len = 2;
	}
	else {
		tag[0] = EL_TYPE_COPY_2 + ((len - 1) << 2);
		tag[1] = offset & 0xFF;
		tag[2] = (offset >> 8) & 0xFF;
		tag_len = 3;
	}

	write_output_buffer(output, tag, tag_len);
}

/**
 * Emit copy elements in chunks of length 64-bytes.
 *
 * @param output: holds output buffer information
 * @param offset: offset of the copy
 * @param len: length of the copy
 */
static void emit_copy(struct out_buffer_context *output, uint32_t offset, uint32_t len) 
{
	//printf("emit_copy %d %d %d\n", offset, len, output->curr);

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
 * @return Resulting compressed size
 */
static uint32_t compress_block(struct in_buffer_context *input, struct out_buffer_context *output, uint32_t input_size, uint16_t *table, uint32_t table_size)
{
	uint32_t output_start = output->curr;
	uint32_t base_input = input->curr;
	uint32_t input_end = input->curr + input_size;
	const int32_t shift = 32 - log2_floor(table_size);

	/*
	 * Bytes in [next_emit, input->curr) will be emitted as literal bytes.
	 * Or [next_emit, input_end) after the main loop.
	 */
	uint32_t next_emit = input->curr;
	const uint32_t input_margin_bytes = 15;

	if (input_size >= input_margin_bytes) {
		const uint32_t input_limit = input->curr + input_size - input_margin_bytes;
		
		uint32_t next_hash;
		for (next_hash = hash(input, ++input->curr, shift);;) {
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
			uint32_t next_input = input->curr;
			uint32_t candidate;
			do {
				input->curr = next_input;
				uint32_t hval = next_hash;
				uint32_t bytes_between_hash_lookups = skip_bytes++ >> 5;
				next_input = input->curr + bytes_between_hash_lookups;

				if (next_input > input_limit) {
					if (next_emit < input_end)
						emit_literal(input, output, input_end - next_emit);
					
					input->curr = input_end;
					return (output->curr - output_start);
				}		

				next_hash = hash(input, next_input, shift);
				candidate = base_input + table[hval];
				table[hval] = input->curr - base_input;
			} while (read_uint32(input, input->curr) != read_uint32(input, candidate));
			
			/*
			 * Step 2: A 4-byte match has been found.  We'll later see if more
			 * than 4 bytes match.	But, prior to the match, input bytes
			 * [next_emit, input->curr) are unmatched.	Emit them as "literal bytes."
			 */
			emit_literal(input, output, input->curr - next_emit);

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
			uint32_t insert_tail;

			do {
				/*
				 * We have a 4-byte match at input->curr, and no need to emit any
				 *	"literal bytes" prior to input->curr.
				 */
				const uint32_t base = input->curr;
				int32_t matched = 4 + find_match_length(input, candidate + 4, input->curr + 4, input_end);
				input->curr += matched;
				advance_seqread(input, matched);
					
				int32_t offset = base - candidate;
				emit_copy(output, offset, matched);
			
				/*
				 * We could immediately start working at input->curr now, but to improve
				 * compression we first update table[Hash(input->curr - 1, ...)]/
				 */
				insert_tail = input->curr - 1;
				next_emit = input->curr;
				if (input->curr >= input_limit) {
					if (next_emit < input_end)
						emit_literal(input, output, input_end - next_emit);
					
					input->curr = input_end;
					return (output->curr - output_start);
				}

				uint32_t prev_hash = hash(input, insert_tail, shift);
				table[prev_hash] = input->curr - base_input - 1;

				uint32_t curr_hash = hash(input, insert_tail + 1, shift);
				candidate = base_input + table[curr_hash];
				table[curr_hash] = input->curr - base_input;
			} while(read_uint32(input, insert_tail + 1) == read_uint32(input, candidate));

			next_hash = hash(input, insert_tail + 2, shift);
			input->curr++;
		}
	}

	// We should never reach this point
	return 0;
}

/************ Public Functions *************/

snappy_status dpu_compress(struct in_buffer_context *input, struct out_buffer_context *output, __mram_ptr uint32_t *header_buffer, uint32_t block_size)
{
	// Allocate the hash table for compression
	uint16_t *table = (uint16_t *)mem_alloc(sizeof(uint16_t) * MAX_HASH_TABLE_SIZE);

	bool idx = 0;
	uint32_t compr_length[2];
	uint32_t length_remain = input->length;
	while (input->curr < input->length) {
		// Get the next block size to compress
		uint32_t to_compress = MIN(length_remain, block_size);

		// Get the size of the hash table used for this block
		uint32_t table_size;
		get_hash_table(table, to_compress, &table_size);
		
		// Compress the current block
		uint32_t compressed_len = compress_block(input, output, to_compress, table, table_size);
		
		// Write out the compressed length of this block
		compr_length[idx] = compressed_len;
		if (idx) {
			mram_write(compr_length, header_buffer, 8);
			header_buffer += 2;
		}
		
		idx = !idx; 
		length_remain -= to_compress;
	}

	// Write out the last compressed length
	if (idx) 
		mram_write(compr_length, header_buffer, 8);

	// Write out last buffer to MRAM
	output->length = output->curr;
	if (output->append_window < output->length) {
		uint32_t len_final = ALIGN(output->length % OUT_BUFFER_LENGTH, 8);
		if (len_final == 0)
			len_final = OUT_BUFFER_LENGTH;

		dbg_printf("Writing window at: 0x%x (%u bytes)\n", output->append_window, len_final);
		mram_write(output->append_ptr, &output->buffer[output->append_window], len_final);
	}

	return SNAPPY_OK;
}
