/**
 * DPU-compatible port of lz4 decompression. Heavily borrowed from
 * https://github.com/andikleen/lz4-c
 */

#include <assert.h>
#include <string.h>  // memcpy
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <mram.h>
#include <defs.h>
#include "dpu_decompress.h"

/*******************
 * Memory helpers  *
 *******************/

/**
 * Read the next input byte from the sequential reader.
 *
 * @param _i: holds input buffer information
 * @return Byte that was read
 */
static inline uint8_t READ_uint8_t(struct in_buffer_context *_i)
{
	uint8_t ret = *_i->ptr;
	_i->ptr = seqread_get(_i->ptr, sizeof(uint8_t), &_i->sr);
	_i->curr++;
	return ret;
}

/**
 * Advance the sequential reader by some amount.
 *
 * @param input: holds input buffer information
 * @param len: number of bytes to advance seqential reader by
 */
static inline void advance_seqread(struct in_buffer_context *input, uint32_t len)
{
	if (len == SEQREAD_CACHE_SIZE) {
		input->ptr = seqread_get(input->ptr, 1, &input->sr);
		input->ptr = seqread_get(input->ptr, SEQREAD_CACHE_SIZE - 1, &input->sr);
	}
	else 
		input->ptr = seqread_get(input->ptr, len, &input->sr);

	input->curr += len;
}

static uint16_t LZ4_readLE16(struct in_buffer_context *input)
{
	return (uint16_t) ((uint16_t) READ_uint8_t(input) + (READ_uint8_t(input)<<8));
}

/***************************
 * Reader & writer helpers *
 ***************************/

/**
 * Copy and append data from the input buffer to the output buffer.
 *
 * @param input: holds input buffer information
 * @param output: holds output buffer information
 * @param len: length of data to copy over
 */
static void writer_append_dpu(struct in_buffer_context *input, struct out_buffer_context *output, uint16_t len)
{
	uint32_t curr_index = output->curr - output->append_window;
	while (len)
	{
		// If we are past the window, write the current window back to MRAM and start a new one
		if (curr_index >= OUT_BUFFER_LENGTH)
		{
			dbg_printf("Past EOB - writing back output %d\n", output->append_window);
			mram_write(output->append_ptr, &output->buffer[output->append_window], OUT_BUFFER_LENGTH);
			output->append_window += OUT_BUFFER_LENGTH;
			curr_index = 0;
		}

		uint32_t to_copy = MIN(OUT_BUFFER_LENGTH - curr_index, len);

		memcpy(&output->append_ptr[curr_index], input->ptr, to_copy);
		output->curr += to_copy;
		len -= to_copy;
		curr_index += to_copy;

		// Advance sequential reader
		advance_seqread(input, to_copy);
	}
}


/**
 * Copy and append previous data to the output buffer. The data may
 * already be existing in the append buffer or read buffer in WRAM,
 * or may need to be copied into the read buffer first.
 *
 * @param output: holds output buffer information
 * @param copy_length: length of data to copy over
 * @param offset: where to copy from, offset from the current output pointer
 * @return False if offset is invalid, True otherwise
 */
static bool write_copy_dpu(struct out_buffer_context *output, uint32_t copy_length, uint32_t offset)
{
	// We only copy previous data, not future data
	if (offset > output->curr)
	{
		printf("Invalid offset detected: 0x%x\n", offset);
		return false;
	}

	uint32_t read_index = output->curr - offset;
	dbg_printf("Copying %u bytes from offset=0x%x to 0x%x\n", copy_length, read_index, output->curr);

	uint8_t *read_ptr;
	uint32_t curr_index = output->curr - output->append_window;
	while (copy_length)
	{
		// if we are past the append window, write the current window back to MRAM and start a new one
		if (curr_index >= OUT_BUFFER_LENGTH)
		{
			dbg_printf("Past EOB - writing back output %d\n", output->append_window);
			mram_write(output->append_ptr, &output->buffer[output->append_window], OUT_BUFFER_LENGTH);
			output->append_window += OUT_BUFFER_LENGTH;
			curr_index = 0;
		}

		uint32_t to_copy = MIN(OUT_BUFFER_LENGTH - curr_index, copy_length);

		// First check if we can use data already in the append window
		if (read_index >= output->append_window) {
			read_ptr = &output->append_ptr[read_index % OUT_BUFFER_LENGTH];
		}
		else {
			if ((read_index + to_copy) > output->append_window)
				to_copy = output->append_window - read_index;
			uint32_t index_offset = read_index - WINDOW_ALIGN(read_index, 8);
			mram_read(&output->buffer[read_index - index_offset], output->read_buf, ALIGN(to_copy + index_offset, 8));
			read_ptr = output->read_buf + index_offset;
		}		
		
		memcpy(&output->append_ptr[curr_index], read_ptr, to_copy);
		output->curr += to_copy;
		copy_length -= to_copy;
		curr_index += to_copy;
		read_index += to_copy; 
	}
	
	return true;
}


/**
 * Copy and append data from the input buffer to the output buffer without advancing seqread,
 * as we are just copying a match that overlaps into upcoming input data.
 *
 * @param output: holds output buffer information
 * @param offset: offset from current output ptr
 * @param matchlength: number of bytes to copy
 */
static void write_overlap(struct out_buffer_context *output, uint32_t offset, uint32_t matchlength)
{
	// First we copy up to the 
	uint32_t to_write = matchlength;
	while(to_write > offset) {
		write_copy_dpu(output, offset, offset);
		to_write -= offset;
	}
	write_copy_dpu(output, to_write, offset);
}


/* Read the variable-length literal or match length.
 *
 * ip - pointer to use as input.
 * lencheck - end ip.  Return an error if ip advances >= lencheck.
 * loop_check - check ip >= lencheck in body of loop.  Returns loop_error if so.
 * initial_check - check ip >= lencheck before start of loop.  Returns initial_error if so.
 * error (output) - error code.  Should be set to 0 before call.
 */
typedef enum { loop_error = -2, initial_error = -1, ok = 0 } variable_length_error;
static inline unsigned
read_variable_length(struct in_buffer_context *input, const uint32_t lencheck,
                     int loop_check, int initial_check,
                     variable_length_error* error)
{
    uint32_t length = 0;
    uint32_t s;
    if (initial_check && input->curr >= lencheck) {    /* overflow detection */
        *error = initial_error;
        return length;
    }
    do {
		s = READ_uint8_t(input);
        length += s;
        if (loop_check && input->curr >= lencheck) {    /* overflow detection */
			*error = loop_error;
            return length;
        }
    } while (s==255);

    return length;
}


/*********************
 * Public functions  *
 *********************/
lz4_status dpu_uncompress(struct in_buffer_context *input, struct out_buffer_context *output)
{
	
	// Read the compressed block size
	uint32_t srcSize = READ_uint8_t(input) |
					(READ_uint8_t(input) << 8) |
					(READ_uint8_t(input) << 16) |
					(READ_uint8_t(input) << 24);
	//printf("%d\n", srcSize);

	uint32_t src = input->curr;
	uint32_t dst = output->curr;
	uint32_t outputSize = output->length;

	if (outputSize < 0) {return -1; }

	{	const uint32_t ip = src;
		const uint32_t iend = ip + srcSize; // VARINT takes 2B

		uint32_t const oend = output->curr + outputSize;
		uint32_t cpy;

		uint32_t const shortiend = iend - 14 /*maxLL*/ - 2 /*offset*/;
        uint32_t const shortoend = oend - 14 /*maxLL*/ - 18 /*maxML*/;

		uint32_t match;
		size_t offset;
		unsigned token;
		size_t length;

		if (srcSize == 0) {return -1;}

		if ((oend - output->curr) < FASTLOOP_SAFE_DISTANCE) {
			goto safe_decode;
		}

		/* Fast Loop : decode sequences as long as output < iend-FASTLOOP_SAFE_DISTANCE */
		while (1) {
			token = READ_uint8_t(input);
			length = token >> ML_BITS;

			if (length == RUN_MASK) {
				variable_length_error error = ok;
				length += read_variable_length(input, iend-RUN_MASK, 1, 1, &error);
				if (error == initial_error) { goto _output_error; }
				/* copy literals */
				cpy = output->curr+length;
				if ((cpy>oend-32) || (input->curr+length>iend-32)) { goto safe_literal_copy; }
                //LZ4_wildCopy32_io(input, output, cpy);
				writer_append_dpu(input, output, length);
			} else {
				cpy = output->curr + length;
				if (ip > iend-(16 + 1/*max lit + offset + nextToken*/)) { goto safe_literal_copy; }
				/* Literals can only be 14, but hope compilers optimize if we copy by a register size */
				writer_append_dpu(input, output, length);
			}

			/* get offset */
			offset = LZ4_readLE16(input); 
			match = output->curr - offset;

			/* get matchlength */
			length = token & ML_MASK;

			if (length == ML_MASK) {
				variable_length_error error = ok;
				length += read_variable_length(input, iend - LASTLITERALS + 1, 1, 0, &error);
                if (error != ok) { goto _output_error; }
                length += MINMATCH;
                if (output->curr + length >= oend - FASTLOOP_SAFE_DISTANCE) {
                    goto safe_match_copy;
				}
            } else {
				length += MINMATCH;
				
				if (output->curr + length >= oend - FASTLOOP_SAFE_DISTANCE) {
					goto safe_match_copy;
				}
			}

			/* Match may extend past output->curr, so we may have to append from input */
			if ((int)(length - offset) > 0) {
				write_overlap(output, offset, length);
			} else {
				//LZ4_wildCopy32(output, match, cpy);
				write_copy_dpu(output, length, offset);
			}
		}
	safe_decode:

		/* Main Loop : decode remaining sequences where output < FASTLOOP_SAFE_DISTANCE */
		while(1) {
			token = READ_uint8_t(input);
			length = token >> ML_BITS; /* literal length */
			/* A two-stage shortcut for the most common case:
             * 1) If the literal length is 0..14, and there is enough space,
             * enter the shortcut and copy 16 bytes on behalf of the literals
             * (in the fast mode, only 8 bytes can be safely copied this way).
             * 2) Further if the match length is 4..18, copy 18 bytes in a similar
             * manner; but we ensure that there's enough space in the output for
             * those 18 bytes earlier, upon entering the shortcut (in other words,
             * there is a combined check for both stages).
             */
            if ( length != RUN_MASK
                /* strictly "less than" on input, to re-enter the loop with at least one byte */
              && (input->curr < shortiend) & (output->curr <= shortoend)) {
                /* Copy the literals */
                writer_append_dpu(input, output, length);
				
                /* The second stage: prepare for match copying, decode full info.
                 * If it doesn't work out, the info won't be wasted. */
                length = token & ML_MASK; /* match length */
                offset = LZ4_readLE16(input);
                match = output->curr - offset;
                
				/* Do not deal with overlapping matches. */
                if ( (length != ML_MASK)
                  && (offset >= 8)
                  && ( match >= dst) ) {
                    /* Copy the match. */
					write_copy_dpu(output, length + MINMATCH, offset);
					
					/* Both stages worked, load the next token. */
                    continue;
                }

                /* The second stage didn't work out, but the info is ready.
                 * Propel it right to the point of match copying. */
                goto _copy_match;
            }

            /* decode literal length */
            if (length == RUN_MASK) {
                variable_length_error error = ok;
                length += read_variable_length(input, iend-RUN_MASK, 1, 1, &error);
                if (error == initial_error) { goto _output_error; }
                if ((uintptr_t)(output->curr)+length<(uintptr_t)(output->curr)) { goto _output_error; } /* overflow detection */
                if ((uintptr_t)(input->curr)+length<(uintptr_t)(input->curr)) { goto _output_error; } /* overflow detection */
            }

            /* copy literals */
            cpy = output->curr + length;

	safe_literal_copy: 
            if ( (((cpy>oend-MFLIMIT) || (input->curr+length>iend-(2+1+LASTLITERALS)))) )
            {
                /* We've either hit the input parsing restriction or the output parsing restriction.
                 * In the normal scenario, decoding a full block, it must be the last sequence,
                 * otherwise it's an error (invalid input or dimensions).
                 * In partialDecoding scenario, it's necessary to ensure there is no buffer overflow.
                 */
				/* We must be on the last sequence because of the parsing limitations so check
					* that we exactly regenerate the original size (must be exact when !endOnInput).
					*/
				if (((input->curr+length != iend) || (cpy > oend))) {
					goto _output_error;
				}
    
	            writer_append_dpu(input, output, length);  /* supports overlapping memory regions; only matters for in-place decompression scenarios */
                
				/* Necessarily EOF when !partialDecoding.
                 * When partialDecoding, it is EOF if we've either
                 * filled the output buffer or
                 * can't proceed with reading an offset for following match.
                 */
                if ((cpy == oend) || (input->curr >= (iend-2))) {
                    break;
                }
            } else {
                writer_append_dpu(input, output, length);   /* may overwrite up to WILDCOPYLENGTH beyond cpy */
            }

            /* get offset */
            offset = LZ4_readLE16(input);
            match = output->curr - offset;

            /* get matchlength */
            length = token & ML_MASK;

    _copy_match:
			if (length == ML_MASK) {
              variable_length_error error = ok;
              length += read_variable_length(input, iend - LASTLITERALS + 1, 1, 0, &error);
              if (error != ok) goto _output_error;
            }
            length += MINMATCH;

        safe_match_copy:
			write_copy_dpu(output, length, offset);
        }
		
		// Write out the final buffer
		if (output->append_window < output->length) {
			uint32_t len_final = output->length % OUT_BUFFER_LENGTH;
			if (len_final == 0)
			len_final = OUT_BUFFER_LENGTH;

			dbg_printf("Writing window at: 0x%x (%u bytes)\n", output->append_window, len_final);
			mram_write(output->append_ptr, &output->buffer[output->append_window], len_final);
		}
		return LZ4_OK;

        /* Overflow error detected */
    _output_error:
		return LZ4_ERROR;
	}
}

