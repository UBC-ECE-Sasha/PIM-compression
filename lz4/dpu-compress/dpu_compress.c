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
 * WRAM space in bytes remaining per tasklet after allocated
 * buffers and stack are accounted for.
 */
#define WRAM_PER_TASKLET ((65536 / NR_TASKLETS) - (2 * OUT_BUFFER_LENGTH) - STACK_SIZE_DEFAULT)

static const uint32_t LZ4_skipTrigger = 6;

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
	input->curr += len;
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
 * Update the token value to include match length. Must first read 8
 * bytes at output_offset, add in the new token value, and then write the buffer back.
 *
 * @param output: holds output buffer information
 * @param offset: offset from start of output buffer to write to
 * @param token_val: match length value that is stored in the token
 */
static void update_token(struct out_buffer_context *output, uint32_t offset, uint8_t token_val) 
{
	if (offset < output->append_window) {
		
		uint8_t data_read[16];
		uint8_t* aligned_read;
		uint32_t aligned_offset = WINDOW_ALIGN(offset, 8);
		aligned_read = (uint8_t*) ALIGN(data_read, 8);
		
		/* Have read 8B into WRAM */
		mram_read(&output->buffer[aligned_offset], aligned_read, 8);

		offset &= 0x7;

		aligned_read[offset] = token_val;
		
		// Write the buffer back
		mram_write(aligned_read, &output->buffer[aligned_offset], 8);
	} else {
		output->append_ptr[offset-output->append_window] = token_val;
	}	
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
		// If we are past the append window, write out current window to MRAM and start a new one
		if (curr_index >= OUT_BUFFER_LENGTH) {
			dbg_printf("Past EOB - writing back output %d\n", output->append_window);
			mram_write(output->append_ptr, &output->buffer[output->append_window], OUT_BUFFER_LENGTH);
			output->append_window += OUT_BUFFER_LENGTH;
			curr_index -= OUT_BUFFER_LENGTH;
		}

		uint32_t to_write = MIN(OUT_BUFFER_LENGTH - curr_index, len);
		memcpy(&output->append_ptr[curr_index], arr, to_write);

		len -= to_write;
		curr_index += to_write;
		output->curr += to_write;
		arr += to_write;
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
		// If we are past the append window, write out current window to MRAM and start a new one
		if (curr_index >= OUT_BUFFER_LENGTH) {
			dbg_printf("Past EOB - writing back output %d\n", output->append_window);

			mram_write(output->append_ptr, &output->buffer[output->append_window], OUT_BUFFER_LENGTH);
			output->append_window += OUT_BUFFER_LENGTH;
			curr_index -= OUT_BUFFER_LENGTH;
		}

		uint32_t to_copy = MIN(OUT_BUFFER_LENGTH - curr_index, len);
		memcpy(&output->append_ptr[curr_index], input->ptr, to_copy);

		// Advance sequential reader
		advance_seqread(input, to_copy);
		
		len -= to_copy;
		curr_index += to_copy;
		output->curr += to_copy;
	}
}

/**
 * Emit match length into output buffer 
 * @param output: holds output buffer information
 * @param len: length of the literal
 */
static void emit_match_length(struct out_buffer_context *output, uint32_t match_len)
{	
	uint8_t match[17]; // Blocks are 4K
	uint8_t count = 0;

	for (; match_len >= 255 ; match_len-=255) {
		match[count] = 255;
		count++;
	}
	match[count++] = match_len;

	/* Write out token and literal length */
	write_output_buffer(output, match, count);
}


/**
 * Emit token and literals from the current location in the input buffer.
 *
 * @param input: holds input buffer information
 * @param output: holds output buffer information
 * @param len: length of the literal
 */
static uint8_t emit_literal(struct in_buffer_context *input, struct out_buffer_context *output, uint32_t lit_len)
{
	uint8_t token[17]; //blocks are 4K
	uint8_t token_len = 1;

	/* token[0] is the token, remaining optional elements are literal length */
	if (lit_len < RUN_MASK) {
		token[0] = (uint8_t) (lit_len<<ML_BITS);
	} else {
		uint32_t len = lit_len - RUN_MASK;
		uint8_t count = 1;

		token[0] = (RUN_MASK<<ML_BITS);

		for (; len >= 255 ; len-=255) {
			token[count] = 255;
			count++;
		}
		token[count++] = len;

		token_len = count;
	}

	/* Write out token and literal length */
	write_output_buffer(output, token, token_len);

	/* Copy Literals */
	copy_output_buffer(input, output, lit_len);
	return token[0];
}

/**
 * Emit a copy element that is less than 64-bytes in length.
 *
 * @param output: holds output buffer information
 * @param offset: offset of the copy
 */
static void emit_offset(struct out_buffer_context *output, uint32_t offset)
{
	uint8_t tag[2];

	tag[0] = offset;
	tag[1] = (offset>>8);
	
	write_output_buffer(output, tag, 2);
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
 * @param bytes: value we want to hash
 * @param shift: adjusts hash to be within table size
 * @return Hash of four bytes stored at ptr
 */
static inline uint32_t hash(uint32_t bytes, int shift)
{
	uint32_t kmul = 0x1e35a7bd;
	return (bytes * kmul) >> shift;
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
 * Perform LZ4 compression on a block of input data, and save the compressed
 * data to the output buffer.
 *
 * @param input: holds input buffer information
 * @param output: holds output buffer information
 * @param input_size: size of the input to compress
 * @param table: pointer to allocated hash table
 * @param table_size: size of the hash table
 */
static int compress_block(struct in_buffer_context *input, struct out_buffer_context *output, uint32_t input_size, uint16_t *table, uint32_t table_size)
{
	const int32_t shift = 32 - log2_floor(table_size);
	uint32_t ip =  input->curr;	
	uint32_t dest = output->curr;	
	uint32_t base = input -> curr;	


	const uint32_t lowLimit = input-> curr;
	uint32_t const iend = ip + input_size;	
	uint32_t matchlimit = iend - LASTLITERALS;
	uint32_t const mflimitPlusOne = iend - MFLIMIT + 1;
	uint32_t anchor = ip;

	//TODO: Add checks for input of size 0, input < input_margin_bytes...
	
	if (input_size < MFLIMIT + 1) goto _last_literals;

	/* First Byte */
	table[hash(read_uint32(input, ip), shift)] = ip - base;
	uint32_t forwardH = hash(read_uint32(input, ++ip), shift);
	
	for ( ; ; ) {
		uint32_t match;
		uint32_t token;

		uint32_t forwardIp = ip;
		int step = 1;
		int searchMatchNb = 1 << LZ4_skipTrigger;
		do {
			uint32_t h = forwardH;
			ip = forwardIp;
			forwardIp += step;
			step = (searchMatchNb++ >> LZ4_skipTrigger);

			if (forwardIp > mflimitPlusOne) goto _last_literals;

			match = base + table[h];
			forwardH = hash(read_uint32(input, forwardIp), shift);		
			table[h] = ip - base;			
		} while (read_uint32(input, match) != read_uint32(input, ip));	

		while (((ip>anchor) & (match > lowLimit)) && 
		(read_uint32(input, ip - 1) == read_uint32(input, match -1))) { ip--; match--; }

		/* Encode Literals*/
		token = output->curr;
					
		uint8_t token_val = emit_literal(input, output, ip - anchor);	
		
_next_match:
		/* at this stage, the following variables must be correctly set :
         * - ip : at start of LZ operation
         * - match : at start of previous pattern occurrence; can be within current prefix, or within extDict
         * - offset : if maybe_ext_memSegment==1 (constant)
         * - lowLimit : must be == dictionary to mean "match is within extDict"; must be == source otherwise
         * - token and *token : position to write 4-bits for match length; higher 4-bits for literal length supposed already written
    	*/

		/* Encode Offset */
		{	uint32_t offset = ip - match;
			emit_offset(output, offset);
		}

		/* Encode MatchLength */
		{	unsigned matchCode;

			matchCode = find_match_length(input, match + MINMATCH, ip + MINMATCH, matchlimit);
			ip += (size_t)matchCode + MINMATCH;
			advance_seqread(input, matchCode + MINMATCH);

			if (matchCode >= ML_MASK) {
				update_token(output, token, token_val + ML_MASK);
				emit_match_length(output, matchCode - ML_MASK);
			} else
				update_token(output, token, token_val + matchCode);
		}		

		anchor = ip;

		/* Test end of chunk */
		if (ip >= mflimitPlusOne) break;

		/* Fill table */
		table[hash(read_uint32(input, ip-2), shift)] = (ip-2) - base;

		/* Test next position */
		match = base + table[hash(read_uint32(input, ip), shift)];
		table[hash(read_uint32(input, ip), shift)] = ip - base;
		if ( (match+LZ4_DISTANCE_MAX >= ip) 
		&& (read_uint32(input, match) == read_uint32(input, ip)) )
		{ 
			token=output->curr++; 
			token_val = 0;
			update_token(output, token, token_val); 
			goto _next_match; 
		}

		/* Prepare next loop */
		forwardH = hash(read_uint32(input, ++ip), shift);
	}

_last_literals:
	/* Encode Last Literals */

	{	size_t lastRun = (size_t)(iend - anchor);
		emit_literal(input, output, lastRun);
	}

	return (int)(((char*)output->curr) - dest); 
}

/************ Public Functions *************/

lz4_status dpu_compress(struct in_buffer_context *input, struct out_buffer_context *output, uint32_t block_size)
{
	// Calculate hash table size
	uint32_t table_size = 1 << log2_floor(WRAM_PER_TASKLET);
	uint32_t num_table_entries = table_size >> 1;
	
	// Allocate the hash table for compression
	uint16_t *table = (uint16_t *)mem_alloc(table_size);
	
	// Compress the current block
	compress_block(input, output, input->length, table, num_table_entries);

	// Write out last buffer to MRAM
	output->length = output->curr;
	if (output->append_window < output->length) {
		uint32_t len_final = ALIGN(output->length % OUT_BUFFER_LENGTH, 8);
		if (len_final == 0)
			len_final = OUT_BUFFER_LENGTH;

		dbg_printf("Writing window at: 0x%x (%u bytes)\n", output->append_window, len_final);
		mram_write(output->append_ptr, &output->buffer[output->append_window], len_final);
	}

	return LZ4_OK;
}
