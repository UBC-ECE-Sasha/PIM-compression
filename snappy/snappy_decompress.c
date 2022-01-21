#include <dpu.h>
#include <dpu_memory.h>
#include <dpu_log.h>
#include <stdint.h>
#include <stdio.h>

#include "snappy_decompress.h"

#define DPU_DECOMPRESS_PROGRAM "dpu-decompress/decompress.dpu"
#define TOTAL_NR_TASKLETS (NR_DPUS * NR_TASKLETS)

#define LZ4_memcpy(dst, src, size) __builtin_memcpy(dst, src, size)

#define FASTLOOP_SAFE_DISTANCE 64

/* __pack instructions are safer, but compiler specific, hence potentially problematic for some compilers */
/* currently only defined for gcc and icc */
typedef union { U16 u16; U32 u32; reg_t uArch; } __attribute__((packed)) unalign;

static U16 LZ4_read16(const void* ptr) { return ((const unalign*)ptr)->u16; }

static void LZ4_write32(void* memPtr, U32 value) { ((unalign*)memPtr)->u32 = value; }

static const unsigned inc32table[8] = {0, 1, 2,  1,  0,  4, 4, 4};
static const int      dec64table[8] = {0, 0, 0, -1, -4,  1, 2, 3};

#define MEM_INIT(p,v,s)   memset((p),(v),(s))

/* customized variant of memcpy, which can overwrite up to 8 bytes beyond dstEnd */
static inline
void LZ4_wildCopy8(void* dstPtr, const void* srcPtr, void* dstEnd)
{
    BYTE* d = (BYTE*)dstPtr;
    const BYTE* s = (const BYTE*)srcPtr;
    BYTE* const e = (BYTE*)dstEnd;

    do { LZ4_memcpy(d,s,8); d+=8; s+=8; } while (d<e);
}


static unsigned LZ4_isLittleEndian(void)
{
    const union { U32 u; BYTE c[4]; } one = { 1 };   /* don't use static : performance detrimental */
    return one.c[0];
}

static U16 LZ4_readLE16(const void* memPtr)
{
    if (LZ4_isLittleEndian()) {
        return LZ4_read16(memPtr);
    } else {
        const BYTE* p = (const BYTE*)memPtr;
        return (U16)((U16)p[0] + (p[1]<<8));
    }
}


static inline void
LZ4_memcpy_using_offset_base(BYTE* dstPtr, const BYTE* srcPtr, BYTE* dstEnd, const size_t offset)
{
    if (offset < 8) {
        LZ4_write32(dstPtr, 0);   /* silence an msan warning when offset==0 */
        dstPtr[0] = srcPtr[0];
        dstPtr[1] = srcPtr[1];
        dstPtr[2] = srcPtr[2];
        dstPtr[3] = srcPtr[3];
        srcPtr += inc32table[offset];
        LZ4_memcpy(dstPtr+4, srcPtr, 4);
        srcPtr -= dec64table[offset];
        dstPtr += 8;
    } else {
        LZ4_memcpy(dstPtr, srcPtr, 8);
        dstPtr += 8;
        srcPtr += 8;
    }

    LZ4_wildCopy8(dstPtr, srcPtr, dstEnd);
}

/* customized variant of memcpy, which can overwrite up to 32 bytes beyond dstEnd
 * this version copies two times 16 bytes (instead of one time 32 bytes)
 * because it must be compatible with offsets >= 16. */
static inline void
LZ4_wildCopy32(void* dstPtr, const void* srcPtr, void* dstEnd)
{
    BYTE* d = (BYTE*)dstPtr;
    const BYTE* s = (const BYTE*)srcPtr;
    BYTE* const e = (BYTE*)dstEnd;

    do { LZ4_memcpy(d,s,16); LZ4_memcpy(d+16,s+16,16); d+=32; s+=32; } while (d<e);
}

/* LZ4_memcpy_using_offset()  presumes :
 * - dstEnd >= dstPtr + MINMATCH
 * - there is at least 8 bytes available to write after dstEnd */
static inline void
LZ4_memcpy_using_offset(BYTE* dstPtr, const BYTE* srcPtr, BYTE* dstEnd, const size_t offset)
{
    BYTE v[8];

    switch(offset) {
    case 1:
        MEM_INIT(v, *srcPtr, 8);
        break;
    case 2:
        LZ4_memcpy(v, srcPtr, 2);
        LZ4_memcpy(&v[2], srcPtr, 2);
        LZ4_memcpy(&v[4], v, 4);
        break;
    case 4:
        LZ4_memcpy(v, srcPtr, 4);
        LZ4_memcpy(&v[4], srcPtr, 4);
        break;
    default:
        LZ4_memcpy_using_offset_base(dstPtr, srcPtr, dstEnd, offset);
        return;
    }

    LZ4_memcpy(dstPtr, v, 8);
    dstPtr += 8;
    while (dstPtr < dstEnd) {
        LZ4_memcpy(dstPtr, v, 8);
        dstPtr += 8;
    }
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
read_variable_length(const BYTE**ip, const BYTE* lencheck,
                     int loop_check, int initial_check,
                     variable_length_error* error)
{
    U32 length = 0;
    U32 s;
    if (initial_check && (*ip) >= lencheck) {    /* overflow detection */
        *error = initial_error;
        return length;
    }
    do {
        s = **ip;
        (*ip)++;
        length += s;
        if (loop_check && (*ip) >= lencheck) {    /* overflow detection */
            *error = loop_error;
            return length;
        }
    } while (s==255);

    return length;
}


/**
 * Attempt to read a varint from the input buffer. The format of a varint
 * consists of little-endian series of bytes where the lower 7 bits are data
 * and the upper bit is set if there are more bytes to read. Maximum size
 * of the varint is 5 bytes.
 *
 * @param input: holds input buffer information
 * @param val: read value of the varint
 * @return False if all 5 bytes were read and there is still more data to
 *		   read, True otherwise
 */
static inline bool read_varint32(struct host_buffer_context *input, uint32_t *val)
{
	int shift = 0;
	*val = 0;

	for (uint8_t count = 0; count < 5; count++) {
		int8_t c = (int8_t)(*input->curr++);
		*val |= (c & BITMASK(7)) << shift;
		if (!(c & (1 << 7)))
			return true;
		shift += 7;
	}

	return false;
}

/**
 * Attempt to read a varint from the input buffer. The format of a varint
 * consists of little-endian series of bytes where the lower 7 bits are data
 * and the upper bit is set if there are more bytes to read. Maximum size
 * of the varint is 5 bytes.
 *
 * @param curr: pointer to the current pointer to the buffer
 * @param val: read value of the varint
 * @return False if all 5 bytes were read and there is still more data to
 *		   read, True otherwise
 */
static inline bool read_varint32_dpu(uint8_t **curr, uint32_t *val)
{
	int shift = 0;
	*val = 0;
	uint8_t *cur = *curr;

	for (uint8_t count = 0; count < 5; count++) {
		int8_t c = (int8_t)(*cur++);
		*val |= (c & BITMASK(7)) << shift;
		if (!(c & (1 << 7))) {
			*curr = cur;
			return true;
		}
		shift += 7;
	}

	*curr = cur;

	return false;
}


/**
 * Read an unsigned integer from the input buffer. Increments
 * the current location in the input buffer.
 *
 * @param curr: pointer to the current pointer to the buffer
 * @return Unsigned integer read
 */
static uint32_t read_uint32_dpu(uint8_t **curr)
{
	uint32_t val = 0;
	uint8_t *cur = *curr;
	for (uint8_t i = 0; i < sizeof(uint32_t); i++) {
		val |= (*cur++) << (8 * i);
	}

	*curr = cur;

	return val;
}
		
/**
 * Read the size of the long literal tag, which is used for literals with
 * length greater than 60 bytes.
 *
 * @param input: holds input buffer information
 * @param len: length in bytes of the size to read
 * @return 0 if we reached the end of input buffer, size of literal otherwise
 */
static inline uint32_t read_long_literal_size(struct host_buffer_context *input, uint32_t len)
{
	if ((input->curr + len) >= (input->buffer + input->length))
		return 0;

	uint32_t size = 0;
	for (uint32_t i = 0; i < len; i++) {
		size |= (*input->curr++ << (i << 3));
	}
	return size;
}

/**
 * Read a 1-byte offset tag and return the offset of the copy that is read.
 *
 * @param tag: tag byte to parse
 * @param input: holds input buffer information
 * @return 0 if we reached the end of input buffer, offset of the copy otherwise
 */
static inline uint16_t make_offset_1_byte(uint8_t tag, struct host_buffer_context *input)
{
	if (input->curr >= (input->buffer + input->length))
		return 0;
	return (uint16_t)(*input->curr++) | (uint16_t)(GET_OFFSET_1_BYTE(tag) << 8);
}

/**
 * Read a 2-byte offset tag and return the offset of the copy that is read.
 *
 * @param tag: tag byte to parse
 * @param input: holds input buffer information
 * @return 0 if we reached the end of input buffer, offset of the copy otherwise
 */
static inline uint16_t make_offset_2_byte(uint8_t tag, struct host_buffer_context *input)
{
	UNUSED(tag);

	uint16_t total = 0;
	if ((input->curr + sizeof(uint16_t)) > (input->buffer + input->length))
		return 0;
	else {
		total = (*input->curr & 0xFF) | ((*(input->curr + 1) & 0xFF) << 8);
		input->curr += sizeof(uint16_t);
		return total;
	}
}

/**
 * Read a 4-byte offset tag and return the offset of the copy that is read.
 *
 * @param tag: tag byte to parse
 * @param input: holds input buffer information
 * @return 0 if we reached the end of input buffer, offset of the copy otherwise
 */
static inline uint32_t make_offset_4_byte(uint8_t tag, struct host_buffer_context *input)
{
	UNUSED(tag);

	uint32_t total = 0;
	if ((input->curr + sizeof(uint32_t)) > (input->buffer + input->length))
		return 0;
	else {
		total = (*input->curr & 0xFF) |
				((*(input->curr + 1) & 0xFF) << 8) |
				((*(input->curr + 2) & 0xFF) << 16) |
			 ((*(input->curr + 3) & 0xFF) << 24);
		input->curr += sizeof(uint32_t);
		return total;
	}
}


snappy_status setup_decompression(struct host_buffer_context *input, struct host_buffer_context *output, struct program_runtime *runtime)
{
	struct timeval start;
	struct timeval end;
	gettimeofday(&start, NULL);

	// Read the decompressed length
	uint32_t dlength;
	if (!read_varint32(input, &dlength)) {
		fprintf(stderr, "Failed to read decompressed length\n");
		return SNAPPY_INVALID_INPUT;
	}

	// Check that uncompressed length is within the max we can store
	if (dlength > output->max) {
		fprintf(stderr, "Output length is to big: max=%ld len=%d\n", output->max, dlength);
		return SNAPPY_BUFFER_TOO_SMALL;
	}

	// Allocate output buffer
	output->buffer = malloc(ALIGN(dlength, 8) | BITMASK(11));
	output->curr = output->buffer;
	output->length = dlength;

	gettimeofday(&end, NULL);
	runtime->pre = get_runtime(&start, &end);

	return SNAPPY_OK;
}


snappy_status snappy_decompress_host(struct host_buffer_context *input, struct host_buffer_context *output)
{
	const BYTE* const src = input->curr; //originally const char
	BYTE* const dst = output->curr;
	int srcSize = input->length;
	int outputSize = output->length;

	if ((src == NULL) || (outputSize < 0)) {return -1; }

	{	const BYTE* ip = (const BYTE*) src;
		const BYTE* const iend = ip + srcSize - 2; // VARINT takes 2B

		BYTE* op = (BYTE*) dst;
		BYTE* const oend = op + outputSize;
		BYTE* cpy;

		const BYTE* const shortiend = iend - 14 /*maxLL*/ - 2 /*offset*/;
        const BYTE* const shortoend = oend - 14 /*maxLL*/ - 18 /*maxML*/;

		const BYTE* match;
		size_t offset;
		unsigned token;
		size_t length;

		if (srcSize == 0) {return -1;}

		if ((oend - op) < FASTLOOP_SAFE_DISTANCE) {
			goto safe_decode;
		}

		/* Fast Loop : decode sequences as long as output < iend-FASTLOOP_SAFE_DISTANCE */
		while (1) {
			token = *ip++;
			length = token >> ML_BITS;

			if (length == RUN_MASK) {
				variable_length_error error = ok;
				length += read_variable_length(&ip, iend-RUN_MASK, 1, 1, &error);
				if (error == initial_error) { goto _output_error; }

				/* copy literals */
				cpy = op+length;
				if ((cpy>oend-32) || (ip+length>iend-32)) { goto safe_literal_copy; }
                LZ4_wildCopy32(op, ip, cpy);
			
				ip += length; op = cpy;
			} else {
				cpy = op+length;
				if (ip > iend-(16 + 1/*max lit + offset + nextToken*/)) { goto safe_literal_copy; }
				/* Literals can only be 14, but hope compilers optimize if we copy by a register size */
				LZ4_memcpy(op, ip, 16);

				ip += length; op = cpy;
			}

			/* get offset */
			offset = LZ4_readLE16(ip); ip+=2;
			match = op - offset;
			//printf("offset: %d\n", offset);

			/* get matchlength */
			length = token & ML_MASK;

			if (length == ML_MASK) {
				variable_length_error error = ok;
				length += read_variable_length(&ip, iend - LASTLITERALS + 1, 1, 0, &error);
                if (error != ok) { goto _output_error; }
                length += MINMATCH;
                if (op + length >= oend - FASTLOOP_SAFE_DISTANCE) {
                    goto safe_match_copy;
				}
            } else {
				length += MINMATCH;
				
				if (op + length >= oend - FASTLOOP_SAFE_DISTANCE) {
					goto safe_match_copy;
				}
			}
			
			/* copy match within block */
			cpy = op + length;
			if (offset<16) {
				LZ4_memcpy_using_offset(op, match, cpy, offset);
			} else {
				LZ4_wildCopy32(op, match, cpy);
			}

			op = cpy; /* wildcopy correction */
		}
	safe_decode:

		/* Main Loop : decode remaining sequences where output < FASTLOOP_SAFE_DISTANCE */
		while(1) {
			token = *ip++;
			printf("token: %d, curr: %d, output: %d\n", token, ip - src, op - dst);	
			length = token >> ML_BITS; /* literal length */
			printf("%d\n", length);

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
              && (ip < shortiend) & (op <= shortoend)) {
                /* Copy the literals */
                LZ4_memcpy(op, ip, 16);
                op += length; ip += length;

                /* The second stage: prepare for match copying, decode full info.
                 * If it doesn't work out, the info won't be wasted. */
                length = token & ML_MASK; /* match length */
                offset = LZ4_readLE16(ip); ip += 2;
                match = op - offset;
				printf("length: %d offset: %d output: %d, input: %d\n", length, offset, op - dst, ip-src);
                /* Do not deal with overlapping matches. */
                if ( (length != ML_MASK)
                  && (offset >= 8)
                  && ( match >= dst) ) {
                    /* Copy the match. */
					printf("%d\n", op-dst);
					LZ4_memcpy(op + 0, match + 0, 8);
                    LZ4_memcpy(op + 8, match + 8, 8);
                    LZ4_memcpy(op +16, match +16, 2);
                    op += length + MINMATCH;
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
                length += read_variable_length(&ip, iend-RUN_MASK, 1, 1, &error);
                if (error == initial_error) { goto _output_error; }
                if ((uptrval)(op)+length<(uptrval)(op)) { goto _output_error; } /* overflow detection */
                if ((uptrval)(ip)+length<(uptrval)(ip)) { goto _output_error; } /* overflow detection */
            }

            /* copy literals */
            cpy = op+length;

	safe_literal_copy: 
            if ( (((cpy>oend-MFLIMIT) || (ip+length>iend-(2+1+LASTLITERALS)))) )
            {
                /* We've either hit the input parsing restriction or the output parsing restriction.
                 * In the normal scenario, decoding a full block, it must be the last sequence,
                 * otherwise it's an error (invalid input or dimensions).
                 * In partialDecoding scenario, it's necessary to ensure there is no buffer overflow.
                 */
				/* We must be on the last sequence because of the parsing limitations so check
					* that we exactly regenerate the original size (must be exact when !endOnInput).
					*/
				if (((ip+length != iend) || (cpy > oend))) {
					goto _output_error;
				}
                memmove(op, ip, length);  /* supports overlapping memory regions; only matters for in-place decompression scenarios */
                ip += length;
                op += length;
                /* Necessarily EOF when !partialDecoding.
                 * When partialDecoding, it is EOF if we've either
                 * filled the output buffer or
                 * can't proceed with reading an offset for following match.
                 */
                if ((cpy == oend) || (ip >= (iend-2))) {
                    break;
                }
            } else {
                LZ4_wildCopy8(op, ip, cpy);   /* may overwrite up to WILDCOPYLENGTH beyond cpy */
                ip += length; op = cpy;
            }

            /* get offset */
            offset = LZ4_readLE16(ip); ip+=2;
            match = op - offset;

            /* get matchlength */
            length = token & ML_MASK;

    _copy_match:
			printf("length: %d\n", length);
			if (length == ML_MASK) {
              variable_length_error error = ok;
              length += read_variable_length(&ip, iend - LASTLITERALS + 1, 1, 0, &error);
              if (error != ok) goto _output_error;
            }
            length += MINMATCH;
        
		safe_match_copy:
            /* copy match within block */
            cpy = op + length;
			printf("length: %d, offset: %d\n", length, offset);
            if (offset<8) {
                LZ4_write32(op, 0);   /* silence msan warning when offset==0 */
                op[0] = match[0];
                op[1] = match[1];
                op[2] = match[2];
                op[3] = match[3];
                match += inc32table[offset];
                LZ4_memcpy(op+4, match, 4);
                match -= dec64table[offset];
            } else {
                LZ4_memcpy(op, match, 8);
                match += 8;
            }
            op += 8;

            if (cpy > oend-MATCH_SAFEGUARD_DISTANCE) {
                BYTE* const oCopyLimit = oend - (WILDCOPYLENGTH-1);
                if (cpy > oend-LASTLITERALS) { goto _output_error; } /* Error : last LASTLITERALS bytes must be literals (uncompressed) */
                if (op < oCopyLimit) {
                    LZ4_wildCopy8(op, match, oCopyLimit);
                    match += oCopyLimit - op;
                    op = oCopyLimit;
                }
                while (op < cpy) { *op++ = *match++; }
            } else {
                LZ4_memcpy(op, match, 8);
                if (length > 16)  { LZ4_wildCopy8(op+8, match+8, cpy); }
            }
            op = cpy;   /* wildcopy correction */

        }
		//(int) (((char*)op)-dst) /* Nb of output bytes decoded */

        return 0;    
        /* Overflow error detected */
    _output_error:
        return (int) (-((ip)-src))-1;
	}
}



snappy_status snappy_decompress_dpu(unsigned char *in, size_t in_len, unsigned char *out, size_t *out_len)
{
	struct timeval start;
	struct timeval end;
	gettimeofday(&start, NULL);
	uint8_t *in_curr = in;

	// Calculate workload of each task
	uint32_t dblock_size = BLOCK_SIZE;

	uint8_t *input_start = in_curr;

	uint32_t num_blocks = 1;
	uint32_t input_blocks_per_dpu = (num_blocks + NR_DPUS - 1) / NR_DPUS;
	uint32_t input_blocks_per_task = (num_blocks + TOTAL_NR_TASKLETS - 1) / TOTAL_NR_TASKLETS;

	uint32_t input_offset[NR_DPUS][NR_TASKLETS] = {0};
	uint32_t output_offset[NR_DPUS][NR_TASKLETS] = {0};

	uint32_t dpu_idx = 0;
	uint32_t task_idx = 0;
	uint32_t task_blocks = 0;
	uint32_t total_offset = 0; 
	for (uint32_t i = 0; i < num_blocks; i++) {
		// If we have reached the next DPU's boundary, update the index
		if (i == (input_blocks_per_dpu * (dpu_idx + 1))) {
			dpu_idx++;
			task_idx = 0;
			task_blocks = 0;
		}

		// If we have reached the next task's boundary, log the offset
		// to the input_offset and output_offset arrays. This should roughly
		// evenly divide the work between NR_TASKLETS tasks on NR_DPUS.
		if (task_blocks == (input_blocks_per_task * task_idx)) {
			input_offset[dpu_idx][task_idx] = total_offset;
			output_offset[dpu_idx][task_idx] = i * dblock_size;
			task_idx++;
		}

		// Read the compressed block size
		uint32_t compressed_size = in_len;
		in_curr += compressed_size;
		
		total_offset += compressed_size;	
		task_blocks++;
	}
	in_curr = input_start; // Reset the pointer back to start for copying data to the DPU

	gettimeofday(&end, NULL);

	// Allocate the DPUs
	gettimeofday(&start, NULL);
	struct dpu_set_t dpus;
	struct dpu_set_t dpu_rank;
	struct dpu_set_t dpu;
	DPU_ASSERT(dpu_alloc(NR_DPUS, NULL, &dpus));
	gettimeofday(&end, NULL);

	gettimeofday(&start, NULL);	
	DPU_ASSERT(dpu_load(dpus, DPU_DECOMPRESS_PROGRAM, NULL));
	gettimeofday(&end, NULL);

	// Calculate input length without header and aligned output length
	gettimeofday(&start, NULL);
	uint32_t total_input_length = in_len;
	uint32_t aligned_output_length = ALIGN(*out_len, 8);

	uint32_t input_length;
	uint32_t output_length;

	dpu_idx = 0;
	DPU_RANK_FOREACH(dpus, dpu_rank) {
#ifdef BULK_XFER
		uint32_t largest_input_length = 0;
		uint32_t starting_dpu_idx = dpu_idx;
#endif
		DPU_FOREACH(dpu_rank, dpu) {
			// Check to get rid of array bounds compiler warning
			if (dpu_idx >= NR_DPUS)
				break; 

			// Calculate input and output lengths for each DPU
			if ((dpu_idx != (NR_DPUS - 1)) && (input_offset[dpu_idx + 1][0] != 0)) {
				input_length = input_offset[dpu_idx + 1][0] - input_offset[dpu_idx][0];
				output_length = output_offset[dpu_idx + 1][0] - output_offset[dpu_idx][0];
			}
			else if ((dpu_idx == 0) || (input_offset[dpu_idx][0] != 0)) {
				input_length = total_input_length - input_offset[dpu_idx][0];
				output_length = aligned_output_length - output_offset[dpu_idx][0];
			}
			else {
				input_length = 0;
				output_length = 0;
			}

			DPU_ASSERT(dpu_copy_to(dpu, "input_length", 0, &input_length, sizeof(uint32_t)));
			DPU_ASSERT(dpu_copy_to(dpu, "output_length", 0, &output_length, sizeof(uint32_t)));

#ifdef BULK_XFER
			if (largest_input_length < input_length)
				largest_input_length = input_length;

			// If all prepared transfers have a larger transfer length by some margin then we have reached 
			// the last DPU. Push existing transfers first to prevent segfault of copying too much data
			if ((input_length + 30000) < largest_input_length) {
				DPU_ASSERT(dpu_push_xfer(dpu_rank, DPU_XFER_TO_DPU, "input_buffer", 0, ALIGN(largest_input_length, 8), DPU_XFER_DEFAULT));
				largest_input_length = input_length;
			}

			DPU_ASSERT(dpu_prepare_xfer(dpu, (void *)(in_curr + input_offset[dpu_idx][0])));
#else
			DPU_ASSERT(dpu_copy_to(dpu, "input_offset", 0, input_offset[dpu_idx], sizeof(uint32_t) * NR_TASKLETS));
			DPU_ASSERT(dpu_copy_to(dpu, "output_offset", 0, output_offset[dpu_idx], sizeof(uint32_t) * NR_TASKLETS));
			DPU_ASSERT(dpu_copy_to(dpu, "input_buffer", 0, in_curr + input_offset[dpu_idx][0], ALIGN(input_length,8)));
#endif
			dpu_idx++;
		}

#ifdef BULK_XFER
		DPU_ASSERT(dpu_push_xfer(dpu_rank, DPU_XFER_TO_DPU, "input_buffer", 0, ALIGN(largest_input_length, 8), DPU_XFER_DEFAULT));

		dpu_idx = starting_dpu_idx;
		DPU_FOREACH(dpu_rank, dpu) {
			DPU_ASSERT(dpu_prepare_xfer(dpu, (void *)input_offset[dpu_idx]));
			dpu_idx++;
		}
		DPU_ASSERT(dpu_push_xfer(dpu_rank, DPU_XFER_TO_DPU, "input_offset", 0, sizeof(uint32_t) * NR_TASKLETS, DPU_XFER_DEFAULT));

		dpu_idx = starting_dpu_idx;
		DPU_FOREACH(dpu_rank, dpu) {
			DPU_ASSERT(dpu_prepare_xfer(dpu, (void *)output_offset[dpu_idx]));
			dpu_idx++;
		}
		DPU_ASSERT(dpu_push_xfer(dpu_rank, DPU_XFER_TO_DPU, "output_offset", 0, sizeof(uint32_t) * NR_TASKLETS, DPU_XFER_DEFAULT));
#endif
	}

	gettimeofday(&end, NULL);

	// Launch all DPUs
	int ret = dpu_launch(dpus, DPU_SYNCHRONOUS);
	if (ret != 0)
	{
		DPU_ASSERT(dpu_free(dpus));
		return SNAPPY_INVALID_INPUT;
	}

	// Deallocate the DPUs
	dpu_idx = 0;
	DPU_RANK_FOREACH(dpus, dpu_rank) {
		uint32_t starting_dpu_idx = dpu_idx;
		gettimeofday(&start, NULL);
#ifdef BULK_XFER
		uint32_t largest_output_length = 0;
#endif
		DPU_FOREACH(dpu_rank, dpu) {
			// Get the results back from the DPU
			DPU_ASSERT(dpu_copy_from(dpu, "output_length", 0, &output_length, sizeof(uint32_t)));
			if (output_length != 0) {	
#ifdef BULK_XFER
				if (largest_output_length < output_length)
					largest_output_length = output_length;

				DPU_ASSERT(dpu_prepare_xfer(dpu, (void *)(out + output_offset[dpu_idx][0])));
#else
			DPU_ASSERT(dpu_copy_from(dpu, "output_buffer", 0, out + output_offset[dpu_idx][0], ALIGN(output_length, 8)));
#endif		
			}

			dpu_idx++;
		}
#ifdef BULK_XFER	
		DPU_ASSERT(dpu_push_xfer(dpu_rank, DPU_XFER_FROM_DPU, "output_buffer", 0, ALIGN(largest_output_length, 8), DPU_XFER_DEFAULT));
#endif
	
		gettimeofday(&end, NULL);

		// Print the logs
		dpu_idx = starting_dpu_idx;
		DPU_FOREACH(dpu_rank, dpu) {
			printf("------DPU %d Logs------\n", dpu_idx);
			DPU_ASSERT(dpu_log_read(dpu, stdout));
			dpu_idx++;
		}	
	}

	gettimeofday(&start, NULL);
	DPU_ASSERT(dpu_free(dpus));
	gettimeofday(&end, NULL);
	
	return SNAPPY_OK;
}	
