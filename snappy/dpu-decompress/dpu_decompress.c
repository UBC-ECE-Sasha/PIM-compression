/**
 * DPU-compatible port of snappy decompression. Heavily borrowed from
 * https://github.com/andikleen/snappy-c
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

static const unsigned inc32table[8] = {0, 1, 2,  1,  0,  4, 4, 4};
static const int      dec64table[8] = {0, 0, 0, -1, -4,  1, 2, 3};

/**
 * Read the next input byte from the sequential reader.
 *
 * @param _i: holds input buffer information
 * @return Byte that was read
 */
static inline uint8_t READ_BYTE(struct in_buffer_context *_i)
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

static U16 LZ4_readLE16(struct in_buffer_context *input)
{
	return (U16) ((U16) READ_BYTE(input) + (READ_BYTE(input)<<8));
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
	printf("g %d %d %d\n", output->curr, output->append_window, len);
	while (len)
	{
		// If we are past the window, write the current window back to MRAM and start a new one
		if (curr_index >= OUT_BUFFER_LENGTH)
		{
			dbg_printf("Past EOB - writing back output %d\n", output->append_window);
			mram_write(output->append_ptr, &output->buffer[output->append_window], OUT_BUFFER_LENGTH);
			printf("curr_i %d %d\n", curr_index, output->append_window);
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

/* customized variant of memcpy, which can overwrite up to 8 bytes beyond dstEnd */
static inline
void LZ4_wildCopy8_io(struct in_buffer_context *input, struct out_buffer_context *output, U32 end)
{
	U32 s = input->curr;
	U32 d = output->curr;
	do { writer_append_dpu(input,output,8); d+=8; s+=8; } while (d<end);
}

/* customized variant of memcpy, which can overwrite up to 8 bytes beyond dstEnd */
static inline
void LZ4_wildCopy8(struct out_buffer_context *output, U32 src, U32 end)
{
	U32 s = src;
	U32 d = output->curr;
	do { write_copy_dpu(output, 8, output->curr - src); d+=8; s+=8; } while (d<end);
}


/* customized variant of memcpy, which can overwrite up to 32 bytes beyond dstEnd
 * this version copies two times 16 bytes (instead of one time 32 bytes)
 * because it must be compatible with offsets >= 16. */
static inline void
LZ4_wildCopy32_io(struct in_buffer_context *input, struct out_buffer_context *output, U32 end)
{
    U32 s = input->curr;
	U32 d = output->curr;
	do { writer_append_dpu(input,output,16); writer_append_dpu(input,output,16); d+=32; s+=32; } while (d<end);
}

/* customized variant of memcpy, which can overwrite up to 8 bytes beyond dstEnd */
static inline
void LZ4_wildCopy32(struct out_buffer_context *output, U32 src, U32 end)
{
	U32 s = src;
	U32 d = output->curr;
	do { write_copy_dpu(output, 32, output->curr - src); d+=32; s+=32; } while (d<end);
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
read_variable_length(struct in_buffer_context *input, const U32 lencheck,
                     int loop_check, int initial_check,
                     variable_length_error* error)
{
    U32 length = 0;
    U32 s;
    if (initial_check && input->curr >= lencheck) {    /* overflow detection */
        *error = initial_error;
        return length;
    }
    do {
		s = READ_BYTE(input);
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
snappy_status dpu_uncompress(struct in_buffer_context *input, struct out_buffer_context *output)
{
	dbg_printf("curr: %u length: %u\n", input->curr, input->length);
	dbg_printf("output length: %u\n", output->length);

	U32 src = input->curr;
	U32 dst = output->curr;
	U32 srcSize = input->length;
	U32 outputSize = output->length;

	if (outputSize < 0) {return -1; }

	{	const U32 ip = src;
		const U32 iend = ip + srcSize - 2; // VARINT takes 2B

		U32 const oend = output->curr + outputSize;
		U32 cpy;

		U32 const shortiend = iend - 14 /*maxLL*/ - 2 /*offset*/;
        U32 const shortoend = oend - 14 /*maxLL*/ - 18 /*maxML*/;

		U32 match;
		size_t offset;
		unsigned token;
		size_t length;

		if (srcSize == 0) {return -1;}

		if ((oend - output->curr) < FASTLOOP_SAFE_DISTANCE) {
			goto safe_decode;
		}

		/* Fast Loop : decode sequences as long as output < iend-FASTLOOP_SAFE_DISTANCE */
		while (1) {
			token = READ_BYTE(input);
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

			//printf("%d %d\n", length, input->curr);

			/* get offset */
			offset = LZ4_readLE16(input); 
			match = output->curr - offset;
			//printf("%d\n", offset);

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

			//printf("%d\n", length);
			
			/* Match may extend past output->curr, so we may have to append from input */
			if ((int)(length - offset) > 0) {
				//if (length - offset < 0) write_copy_dpu(output, length, offset); 
				write_copy_dpu(output, offset, offset);
				writer_append_dpu(input, output, length - offset);
			} else {
				//LZ4_wildCopy32(output, match, cpy);
				write_copy_dpu(output, length, offset);
			}
		}
	safe_decode:

		/* Main Loop : decode remaining sequences where output < FASTLOOP_SAFE_DISTANCE */
		while(1) {
			token = READ_BYTE(input);
			printf("token: %d %d %d\n", token, input->curr, output->curr);
			length = token >> ML_BITS; /* literal length */
			printf("%d\n", length);
			U32 op = output->curr;
			U32 ip = input->curr;
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
				printf("%d %d %d %d\n", length, offset, output->curr, input->curr);
                /* Do not deal with overlapping matches. */
                if ( (length != ML_MASK)
                  && (offset >= 8)
                  && ( match >= dst) ) {
                    /* Copy the match. */
					printf("%d\n", output->curr);
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
                if ((uptrval)(output->curr)+length<(uptrval)(output->curr)) { goto _output_error; } /* overflow detection */
                if ((uptrval)(input->curr)+length<(uptrval)(input->curr)) { goto _output_error; } /* overflow detection */
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
			printf("%d\n", length);
			if (length == ML_MASK) {
              variable_length_error error = ok;
              length += read_variable_length(input, iend - LASTLITERALS + 1, 1, 0, &error);
              if (error != ok) goto _output_error;
            }
            length += MINMATCH;

        safe_match_copy:
            /* copy match within block */
            cpy = output->curr + length;
			printf("%d %d\n", length, offset);

            if (offset<8) {
				write_copy_dpu(output, 4, output->curr - match);
                match += inc32table[offset];
                write_copy_dpu(output, 4, output->curr - match);
                match -= dec64table[offset];
            } else {
                write_copy_dpu(output, 8, output->curr - match);
                match += 8;
            }

            if (cpy > oend-MATCH_SAFEGUARD_DISTANCE) {
                U32 const oCopyLimit = oend - (WILDCOPYLENGTH-1);
                if (cpy > oend-LASTLITERALS) { goto _output_error; } /* Error : last LASTLITERALS bytes must be literals (uncompressed) */
                if (output->curr < oCopyLimit) {
					LZ4_wildCopy8(output, match, oCopyLimit);
					match += oCopyLimit - output->curr;
                    output->curr = oCopyLimit;
                }
                write_copy_dpu(output, cpy - output->curr, output->curr - match);
            } else {
                write_copy_dpu(output, 8, output->curr - match);
                if (length > 16)  { LZ4_wildCopy8(output, match + 8, cpy); }
            }
			output->curr = cpy;

        }
		printf("%d %d\n", output->length, output->append_window);
		// Write out the final buffer
		if (output->append_window < output->length) {
			uint32_t len_final = output->length % OUT_BUFFER_LENGTH;
			if (len_final == 0)
			len_final = OUT_BUFFER_LENGTH;

			dbg_printf("Writing window at: 0x%x (%u bytes)\n", output->append_window, len_final);
			mram_write(output->append_ptr, &output->buffer[output->append_window], len_final);
		}
		return SNAPPY_OK;

        /* Overflow error detected */
    _output_error:
		return SNAPPY_ERROR;
	}
}

