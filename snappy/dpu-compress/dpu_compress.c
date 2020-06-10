#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <mram.h>
#include <defs.h>
#include "alloc.h"

#include "dpu_compress.h"

/**
 * This value could be halfed or quartered to save memory
 * at the cost of slightly worse compression.
 */
#define MAX_HASH_TABLE_BITS 14
#define MAX_HASH_TABLE_SIZE (1U << MAX_HASH_TABLE_BITS)

static inline int32_t log2_floor(uint32_t n)
{
	return (n == 0) ? -1 : 31 ^ __builtin_clz(n);
}

/**
 * Read an unsigned integer from input_buffer in MRAM.
 *
 * @param offset: offset from start of input_buffer to read from
 * @return Value read
 */
static inline uint32_t read_uint32(uint32_t offset)
{
	uint8_t data_read[16];
	mram_read(input_buf + offset, data_read, 16);

	offset %= 8;
	
	return (data_read[offset] |
			(data_read[offset + 1] << 8) |
			(data_read[offset + 2] << 16) |
			(data_read[offset + 3] << 24));
}


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


static void copy_output_buffer(struct in_buffer_context *input, struct out_buffer_context *output, uint32_t len)
{
	uint32_t curr_index = output->curr - output->append_window;
	while (len) {
		uint32_t to_copy = MIN(OUT_BUFFER_LENGTH - curr_index, len);
		memcpy(&output->append_ptr[curr_index], input->ptr, to_copy);

		// Advance sequential reader
		input->ptr = seqread_get(input->ptr, to_copy, &input->sr);
		
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
 * Write a varint to the output buffer. 
 *
 * @param output: holds output buffer information
 * @param val: value to write
 */
static inline void write_varint32(struct out_buffer_context *output, uint32_t val)
{
	static const int mask = 128;
	uint8_t varint[5];
	uint8_t len = 0;

	if (val < (1 << 7)) {
		varint[0] = val;
		len = 1;
	}
	else if (val < (1 << 14)) {
		varint[0] = val | mask;
		varint[1] = val >> 7;
		len = 2;
	}
	else if (val < (1 << 21)) {
		varint[0] = val | mask;
		varint[1] = (val >> 7) | mask;
		varint[2] = val >> 14;
		len = 3;
	}
	else if (val < (1 << 28)) {
		varint[0] = val | mask;
		varint[1] = (val >> 7) | mask;
		varint[2] = (val >> 14) | mask;
		varint[3] = val >> 21;
		len = 4;
	}
	else {
		varint[0] = val | mask;
		varint[1] = (val >> 7) | mask;
		varint[2] = (val >> 14) | mask;
		varint[3] = (val >> 21) | mask;
		varint[4] = val >> 28;
		len = 5;
	}

	// Write out the varint to the output buffer
	write_output_buffer(output, varint, len);
}

/**
 * Write an unsigned integer to the output buffer. 
 *
 * @param ptr: where to write the integer
 * @param val: value to write
 */
static inline void write_uint32(uint8_t *ptr, uint32_t val)
{
	*ptr++ = val & 0xFF;
	*ptr++ = (val >> 8) & 0xFF;
	*ptr++ = (val >> 16) & 0xFF;
	*ptr++ = (val >> 24) & 0xFF;
}

static inline void get_hash_table(uint16_t *table, uint32_t size_to_compress, uint32_t *table_size)
{
	*table_size = 256;
	while ((*table_size < MAX_HASH_TABLE_SIZE) && (*table_size < size_to_compress))
		*table_size <<= 1;

	memset(table, 0, *table_size * sizeof(*table));
}

/**
 * Any hash function will produce a valid compressed bitstream, but a good
 * hash function reduces the number of collisions and thus yields better
 * compression for compressible input, and more speed for incompressible
 * input. Of course, it doesn't hurt if the hash function is reasonably fast
 * either, as it gets called a lot.
 */
static inline uint32_t hash(uint32_t ptr, int shift)
{
	uint32_t kmul = 0x1e35a7bd;
	uint32_t bytes = read_uint32(ptr);
	return (bytes * kmul) >> shift;
}

static inline int32_t find_match_length(uint32_t s1, uint32_t s2, uint32_t s2_limit)
{
	int32_t matched = 0;
	
	// Check by increments of 4 first
	while ((s2 <= (s2_limit - 4)) && (read_uint32(s2) == read_uint32(s1 + matched))) {
		s2 += 4;
		matched += 4;
	}

	// Remaining bytes
	if (s2 <= (s2_limit - 4)) {
		uint32_t x = read_uint32(s1 + matched) ^ read_uint32(s2);
		matched += (__builtin_ctz(x) >> 3);
	}

	return matched;
}

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
			uint32_t next_input = input->curr;
			uint32_t candidate;
			do {
				input->curr = next_input;
				uint32_t hval = next_hash;
				uint32_t bytes_between_hash_lookups = skip_bytes++ >> 5;
				next_input = input->curr + bytes_between_hash_lookups;

				if (next_input > input_limit) {
					emit_literal(input, output, input_end - next_emit);
					input->curr = input_end;
					return (output->curr - output_start);
				}		

				next_hash = hash(next_input, shift);
				candidate = base_input + table[hval];
				table[hval] = input->curr - base_input;
			} while (read_uint32(input->curr) != read_uint32(candidate));
			
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
				int32_t matched = 4 + find_match_length(candidate + 4, input->curr + 4, input_end);
				input->curr += matched;
				input->ptr = seqread_get(input->ptr, matched, &input->sr);
					
				int32_t offset = base - candidate;
				emit_copy(output, offset, matched);
			
				/*
				 * We could immediately start working at input->curr now, but to improve
				 * compression we first update table[Hash(input->curr - 1, ...)]/
				 */
				insert_tail = input->curr - 1;
				next_emit = input->curr;
				if (input->curr >= input_limit) {
					emit_literal(input, output, input_end - next_emit);
					input->curr = input_end;
					return (output->curr - output_start);
				}

				uint32_t prev_hash = hash(insert_tail, shift);
				table[prev_hash] = input->curr - base_input - 1;

				uint32_t curr_hash = hash(insert_tail + 1, shift);
				candidate = base_input + table[curr_hash];
				table[curr_hash] = input->curr - base_input;
			} while(read_uint32(insert_tail + 1) == read_uint32(candidate));

			next_hash = hash(insert_tail + 2, shift);
			input->curr++;
		}
	}

	// We should never reach this point
	return 0;
}

snappy_status dpu_compress(struct in_buffer_context *input, struct out_buffer_context *output, uint32_t block_size)
{
	// Allocate the hash table for compression, TODO: do something about this size
	uint16_t *table = (uint16_t *)mem_alloc(sizeof(uint16_t) * MAX_HASH_TABLE_SIZE);

	// Write the decompressed length
	uint32_t length_remain = input->length;
	write_varint32(output, length_remain);

	// Write the decompressed block size
	write_varint32(output, block_size);

	// Make space for the compressed lengths array
	uint32_t len_compr_lengths = sizeof(uint32_t) * ((input->length + block_size - 1) / block_size);
	uint32_t idx_compr_lengths = output->curr;
	uint8_t *compr_lengths = (uint8_t *)mem_alloc(len_compr_lengths);
	output->curr += len_compr_lengths;

	while (input->curr < input->length) {
		// Get the next block size ot compress
		uint32_t to_compress = MIN(length_remain, block_size);

		// Get the size of the hash table used for this block
		uint32_t table_size;
		get_hash_table(table, to_compress, &table_size);
		
		// Compress the current block
		uint32_t compressed_len = compress_block(input, output, to_compress, table, table_size);
		
		// Write out the compressed length of this block
		write_uint32(compr_lengths, compressed_len);
		compr_lengths += sizeof(uint32_t);
 
		length_remain -= to_compress;
	}
	
	// Update output length
	output->length = output->curr;

	// Write out last buffer to MRAM
	if (output->append_window < output->length) {
		uint32_t len_final = ALIGN(output->length % OUT_BUFFER_LENGTH, 8);
		if (len_final == 0)
			len_final = OUT_BUFFER_LENGTH;

		dbg_printf("Writing window at: 0x%x (%u bytes)\n", output->append_window, len_final);
		mram_write(output->append_ptr, &output->buffer[output->append_window], len_final);
	}

	// Write the compressed lengths buffer to MRAM
	mram_read(&output->buffer[0], output->append_ptr, OUT_BUFFER_LENGTH);
	memcpy(&output->append_ptr[idx_compr_lengths], compr_lengths - len_compr_lengths, len_compr_lengths);
	mram_write(output->append_ptr, &output->buffer[0], OUT_BUFFER_LENGTH);

	return SNAPPY_OK;
}
