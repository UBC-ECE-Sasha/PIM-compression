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

/* Types */
typedef uint8_t BYTE;
typedef uint16_t U16;
typedef uint32_t U32;
typedef uint64_t U64;

typedef U64 reg_t;

/* LZ4 constants */
#define LZ4_DISTANCE_MAX 65535
#define LASTLITERALS 	 5
#define MINMATCH 		 4
#define MFLIMIT 		 12

/* Encoding Constants*/
#define ML_BITS 4
#define ML_MASK ((1U<<ML_BITS)-1)
#define RUN_BITS (8-ML_BITS)
#define RUN_MASK ((1U<<RUN_BITS)-1)

/* Reading and writing to memory */

/**
 * LZ4 relies on memcpy with a constant size being inlined. In freestanding
 * environments, the compiler can't assume the implementation of memcpy() is
 * standard compliant, so it can't apply its specialized memcpy() inlining
 * logic. When possible, use __builtin_memcpy() to tell the compiler to analyze
 * memcpy() as if it were standard compliant, so it can inline it in freestanding
 * environments. This is needed when decompressing the Linux Kernel, for example.
 */
#if defined(__GNUC__) && (__GNUC__ >= 4)
#define LZ4_memcpy(dst, src, size) __builtin_memcpy(dst, src, size)
#else
#define LZ4_memcpy(dst, src, size) memcpy(dst, src, size)
#endif

static unsigned LZ4_isLittleEndian(void)
{
    const union { U32 u; BYTE c[4]; } one = { 1 };   /* don't use static : performance detrimental */
    return one.c[0];
}

/* __pack instructions are safer, but compiler specific, hence potentially problematic for some compilers */
/* currently only defined for gcc and icc */
typedef union { U16 u16; U32 u32; reg_t uArch; } __attribute__((packed)) unalign;

static U16 LZ4_read16(const void* ptr) { return ((const unalign*)ptr)->u16; }
static U32 LZ4_read32(const void* ptr) { return ((const unalign*)ptr)->u32; }
static reg_t LZ4_read_ARCH(const void* ptr) { return ((const unalign*)ptr)->uArch; }

static void LZ4_write16(void* memPtr, U16 value) { ((unalign*)memPtr)->u16 = value; }
static void LZ4_write32(void* memPtr, U32 value) { ((unalign*)memPtr)->u32 = value; }

static const U32 LZ4_skipTrigger = 6;

/*-************************************
*  Error detection
**************************************/
#  ifndef assert
#    define assert(condition) ((void)0)
#  endif


static unsigned LZ4_NbCommonBytes (reg_t val)
{
    assert(val != 0);
    if (LZ4_isLittleEndian()) {
        if (sizeof(val) == 8) {
#       if defined(_MSC_VER) && (_MSC_VER >= 1800) && defined(_M_AMD64) && !defined(LZ4_FORCE_SW_BITCOUNT)
#         if defined(__clang__) && (__clang_major__ < 10)
            /* Avoid undefined clang-cl intrinics issue.
             * See https://github.com/lz4/lz4/pull/1017 for details. */
            return (unsigned)__builtin_ia32_tzcnt_u64(val) >> 3;
#         else
            /* x64 CPUS without BMI support interpret `TZCNT` as `REP BSF` */
            return (unsigned)_tzcnt_u64(val) >> 3;
#         endif
#       elif defined(_MSC_VER) && defined(_WIN64) && !defined(LZ4_FORCE_SW_BITCOUNT)
            unsigned long r = 0;
            _BitScanForward64(&r, (U64)val);
            return (unsigned)r >> 3;
#       elif (defined(__clang__) || (defined(__GNUC__) && ((__GNUC__ > 3) || \
                            ((__GNUC__ == 3) && (__GNUC_MINOR__ >= 4))))) && \
                                        !defined(LZ4_FORCE_SW_BITCOUNT)
            return (unsigned)__builtin_ctzll((U64)val) >> 3;
#       else
            const U64 m = 0x0101010101010101ULL;
            val ^= val - 1;
            return (unsigned)(((U64)((val & (m - 1)) * m)) >> 56);
#       endif
        } else /* 32 bits */ {
#       if defined(_MSC_VER) && (_MSC_VER >= 1400) && !defined(LZ4_FORCE_SW_BITCOUNT)
            unsigned long r;
            _BitScanForward(&r, (U32)val);
            return (unsigned)r >> 3;
#       elif (defined(__clang__) || (defined(__GNUC__) && ((__GNUC__ > 3) || \
                            ((__GNUC__ == 3) && (__GNUC_MINOR__ >= 4))))) && \
                        !defined(__TINYC__) && !defined(LZ4_FORCE_SW_BITCOUNT)
            return (unsigned)__builtin_ctz((U32)val) >> 3;
#       else
            const U32 m = 0x01010101;
            return (unsigned)((((val - 1) ^ val) & (m - 1)) * m) >> 24;
#       endif
        }
    } else   /* Big Endian CPU */ {
        if (sizeof(val)==8) {
#       if (defined(__clang__) || (defined(__GNUC__) && ((__GNUC__ > 3) || \
                            ((__GNUC__ == 3) && (__GNUC_MINOR__ >= 4))))) && \
                        !defined(__TINYC__) && !defined(LZ4_FORCE_SW_BITCOUNT)
            return (unsigned)__builtin_clzll((U64)val) >> 3;
#       else
#if 1
            /* this method is probably faster,
             * but adds a 128 bytes lookup table */
            static const unsigned char ctz7_tab[128] = {
                7, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
                4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
                5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
                4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
                6, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
                4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
                5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
                4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
            };
            U64 const mask = 0x0101010101010101ULL;
            U64 const t = (((val >> 8) - mask) | val) & mask;
            return ctz7_tab[(t * 0x0080402010080402ULL) >> 57];
#else
            /* this method doesn't consume memory space like the previous one,
             * but it contains several branches,
             * that may end up slowing execution */
            static const U32 by32 = sizeof(val)*4;  /* 32 on 64 bits (goal), 16 on 32 bits.
            Just to avoid some static analyzer complaining about shift by 32 on 32-bits target.
            Note that this code path is never triggered in 32-bits mode. */
            unsigned r;
            if (!(val>>by32)) { r=4; } else { r=0; val>>=by32; }
            if (!(val>>16)) { r+=2; val>>=8; } else { val>>=24; }
            r += (!val);
            return r;
#endif
#       endif
        } else /* 32 bits */ {
#       if (defined(__clang__) || (defined(__GNUC__) && ((__GNUC__ > 3) || \
                            ((__GNUC__ == 3) && (__GNUC_MINOR__ >= 4))))) && \
                                        !defined(LZ4_FORCE_SW_BITCOUNT)
            return (unsigned)__builtin_clz((U32)val) >> 3;
#       else
            val >>= 8;
            val = ((((val + 0x00FFFF00) | 0x00FFFFFF) + val) |
              (val + 0x00FF0000)) >> 24;
            return (unsigned)val ^ 3;
#       endif
        }
    }
}

#define STEPSIZE sizeof(reg_t)
unsigned LZ4_count(const BYTE* pIn, const BYTE* pMatch, const BYTE* pInLimit)
{
    const BYTE* const pStart = pIn;

    if (pIn < pInLimit-(STEPSIZE-1)) {
        reg_t const diff = LZ4_read_ARCH(pMatch) ^ LZ4_read_ARCH(pIn);
        if (!diff) {
            pIn+=STEPSIZE; pMatch+=STEPSIZE;
        } else {
            return LZ4_NbCommonBytes(diff);
    }   }

    while (pIn < pInLimit-(STEPSIZE-1)) {
        reg_t const diff = LZ4_read_ARCH(pMatch) ^ LZ4_read_ARCH(pIn);
        if (!diff) { pIn+=STEPSIZE; pMatch+=STEPSIZE; continue; }
        pIn += LZ4_NbCommonBytes(diff);
        return (unsigned)(pIn - pStart);
    }

    if ((STEPSIZE==8) && (pIn<(pInLimit-3)) && (LZ4_read32(pMatch) == LZ4_read32(pIn))) { pIn+=4; pMatch+=4; }
    if ((pIn<(pInLimit-1)) && (LZ4_read16(pMatch) == LZ4_read16(pIn))) { pIn+=2; pMatch+=2; }
    if ((pIn<pInLimit) && (*pMatch == *pIn)) pIn++;
    return (unsigned)(pIn - pStart);
}

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

/* customized variant of memcpy, which can overwrite up to 8 bytes beyond dstEnd */
static void LZ4_wildCopy8(void* dstPtr, const void* srcPtr, void* dstEnd)
{
    BYTE* d = (BYTE*)dstPtr;
    const BYTE* s = (const BYTE*)srcPtr;
    BYTE* const e = (BYTE*)dstEnd;

    do { LZ4_memcpy(d,s,8); d+=8; s+=8; } while (d<e);
}


static void LZ4_writeLE16(void* memPtr, U16 value)
{
    if (LZ4_isLittleEndian()) {
        LZ4_write16(memPtr, value);
    } else {
        BYTE* p = (BYTE*)memPtr;
        p[0] = (BYTE) value;
        p[1] = (BYTE)(value>>8);
    }
}

/*! LZ4_compressBound() :
    Provides the maximum size that LZ4 compression may output in a "worst case" scenario (input data not compressible)
    This function is primarily useful for memory allocation purposes (destination buffer size).
    Note that LZ4_compress_default() compresses faster when dstCapacity is >= LZ4_compressBound(srcSize)
        inputSize  : max supported value is LZ4_MAX_FILE_LENGTH
        return : maximum output size in a "worst case" scenario
              or 0, if input size is incorrect (too large or negative)
*/
static inline uint32_t snappy_max_compressed_length(uint32_t input_length) {
	if (input_length > 0) 
		return input_length + ((input_length)/255) + 16;
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
 * Write a varint to the output buffer. See the decompression code
 * for a description of this format.
 *
 * @param curr: pointer to the current pointer to the buffer
 * @param val: value to write
 */
static inline void write_varint32_dpu(uint8_t **curr, uint32_t val)
{
	static const int mask = 128;
	uint8_t *cur = *curr;

	if (val < (1 << 7)) {
		(*cur++) = val;
	}
	else if (val < (1 << 14)) {
		(*cur++) = val | mask;
		(*cur++) = val >> 7;
	}
	else if (val < (1 << 21)) {
		(*cur++) = val | mask;
		(*cur++) = (val >> 7) | mask;
		(*cur++) = val >> 14;
	}
	else if (val < (1 << 28)) {
		(*cur++) = val | mask;
		(*cur++) = (val >> 7) | mask;
		(*cur++) = (val >> 14) | mask;
		(*cur++) = val >> 21;
	}
	else {
		(*cur++) = val | mask;
		(*cur++) = (val >> 7) | mask;
		(*cur++) = (val >> 14) | mask;
		(*cur++) = (val >> 21) | mask;
		(*cur++) = val >> 28;
	}
	*curr = cur;
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
 * Perform Snappy compression on a block of input data, and save the compressed
 * data to the output buffer.
 *
 * @param input: holds input buffer information
 * @param output: holds output buffer information
 * @param input_size: size of the input to compress
 * @param table: pointer to allocated hash table
 * @param table_size: size of the hash table
 */
static int compress_block(struct host_buffer_context *input, struct host_buffer_context *output, uint32_t input_size, uint16_t *table, uint32_t table_size)
{
	const int32_t shift = 32 - log2_floor(table_size);
	BYTE* ip = (BYTE*) input->curr;	
	char* const dest = output->curr;	
	BYTE* op = (BYTE*) output->curr;
	const BYTE* base = (const BYTE*) input -> curr;	


	const BYTE* lowLimit = (const BYTE*) input-> curr;
	const BYTE* const iend = ip + input_size;	
	const BYTE* matchlimit = iend - LASTLITERALS;
	const BYTE* const mflimitPlusOne = iend - MFLIMIT + 1;
	const BYTE* anchor = (const BYTE*) ip;

	//TODO: Add checks for input of size 0, input < input_margin_bytes...

	if (input_size < MFLIMIT + 1) goto _last_literals;

	/* First Byte */
	table[hash(ip, shift)] = ip - base;
	U32 forwardH = hash(++ip, shift);
	
	for ( ; ; ) {
		BYTE* match;
		BYTE* token;

		BYTE* forwardIp = ip;
		int step = 1;
		int searchMatchNb = 1 << LZ4_skipTrigger;
		do {
			U32 h = forwardH;
			ip = forwardIp;
			forwardIp += step;
			step = (searchMatchNb++ >> LZ4_skipTrigger);

			if (forwardIp > mflimitPlusOne) goto _last_literals;
			assert(ip < mflimitPlusOne);

			match = base + table[h];
			forwardH = hash(forwardIp, shift);		
			table[h] = ip - base;
		} while (read_uint32(match) != read_uint32(ip));


		while (((ip>anchor) & (match > lowLimit)) && (ip[-1]==match[-1])) { ip--; match--; }

		/* Encode Literals*/
		{	unsigned const litLength = (unsigned) (ip - anchor);
			token = op++;
			
			if (litLength >= RUN_MASK) {
				int len = (int) (litLength - RUN_MASK);
				*token = (RUN_MASK<<ML_BITS);
				for(; len >= 255 ; len-=255) *op++ = 255;
				*op++ = (BYTE) len;
			}
			else *token = (BYTE)(litLength<<ML_BITS);

			/* Copy Literals */
			LZ4_wildCopy8(op, anchor, op+litLength);
			op+=litLength;
		}

_next_match:
		/* at this stage, the following variables must be correctly set :
         * - ip : at start of LZ operation
         * - match : at start of previous pattern occurrence; can be within current prefix, or within extDict
         * - offset : if maybe_ext_memSegment==1 (constant)
         * - lowLimit : must be == dictionary to mean "match is within extDict"; must be == source otherwise
         * - token and *token : position to write 4-bits for match length; higher 4-bits for literal length supposed already written
    	*/

		/* Encode Offset */
		assert(ip-match <= LZ4_DISTANCE_MAX);
		LZ4_writeLE16(op, (U16)(ip - match)); op+=2;

		/* Encode MatchLength */
		{	unsigned matchCode;

			matchCode = LZ4_count(ip+MINMATCH, match+MINMATCH, matchlimit);
            ip += (size_t)matchCode + MINMATCH;

			if (matchCode >= ML_MASK) {
				*token += ML_MASK;
				matchCode -= ML_MASK;
				LZ4_write32(op, 0xFFFFFFFF);
			
				while (matchCode >= 4*255) {
					op+=4;
					LZ4_write32(op, 0xFFFFFFFF);
					matchCode -= 4*255;
				}
				op += matchCode / 255;
				*op++ = (BYTE)(matchCode % 255);
			} else
				*token += (BYTE)(matchCode);
		}
		

		anchor = ip;

		/* Test end of chunk */
		if (ip >= mflimitPlusOne) break;

		/* Fill table */
		table[hash(ip-2, shift)] = (ip-2) - base;

		/* Test next position */
		match = base + table[hash(ip, shift)];
		table[hash(ip, shift)] = ip - base;
		if ( (match+LZ4_DISTANCE_MAX >= ip) 
		&& (read_uint32(match) == read_uint32(ip)) )
		{ token=op++; *token=0; goto _next_match; }

		/* Prepare next loop */
		forwardH = hash(++ip, shift);
	}

_last_literals:
	/* Encode Last Literals */

	{	size_t lastRun = (size_t)(iend - anchor);
		if (lastRun >= RUN_MASK) {
			size_t accumulator = lastRun - RUN_MASK;
			*op++ = RUN_MASK << ML_BITS;
			for(; accumulator >= 255 ; accumulator -=255) *op++ = 255;
			*op++ = (BYTE) accumulator;
		} else {
			*op++ = (BYTE)(lastRun<<ML_BITS);
		}
		LZ4_memcpy(op, anchor, lastRun);
		ip = anchor + lastRun;
		op += lastRun;	
	}

	return (int)(((char*)op) - dest);
}
			
/*************** Public Functions *******************/

void setup_compression(struct host_buffer_context *input, struct host_buffer_context *output, struct program_runtime *runtime) 
{
	struct timeval start;
	struct timeval end;
	gettimeofday(&start, NULL);

	uint32_t max_compressed_length = snappy_max_compressed_length(input->length);
	output->buffer = malloc(sizeof(uint8_t) * max_compressed_length);
	output->curr = output->buffer;
	output->length = 0;

	gettimeofday(&end, NULL);
	runtime->pre = get_runtime(&start, &end);
}

snappy_status snappy_compress_host(struct host_buffer_context *input, struct host_buffer_context *output, uint32_t block_size)
{
	// Allocate the hash table for compression
	uint16_t *table = malloc(sizeof(uint16_t) * MAX_HASH_TABLE_SIZE);

	// Write the decompressed length
	uint32_t length_remain = input->length;
	int bytes_compressed = 0;
	//write_varint32(output, length_remain);

	// Write the decompressed block size
	//write_varint32(output, block_size);

	while (input->curr < (input->buffer + input->length) && length_remain > 0) {
		// Get the next block size ot compress
		uint32_t to_compress = MIN(length_remain, block_size);

		// Get the size of the hash table used for this block
		uint32_t table_size;
		get_hash_table(table, to_compress, &table_size);
		
		// Compress the current block
		bytes_compressed += compress_block(input, output, to_compress, table, table_size);
		
		length_remain -= to_compress;
	}

	// Update output length
	output->length = bytes_compressed;

	return SNAPPY_OK;
}

snappy_status snappy_compress_dpu(unsigned char *in, size_t in_len, unsigned char *out, size_t *out_len, void *wrkmem)
{
	// Set block size
	uint32_t block_size = BLOCK_SIZE;
	uint8_t *in_curr = in;
	uint8_t *out_curr = out;

	struct timeval start;
	struct timeval end;
	gettimeofday(&start, NULL);

	// Calculate the workload of each task
	uint32_t num_blocks = (in_len + block_size - 1) / block_size;
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
	write_varint32_dpu(&out_curr, in_len);
	write_varint32_dpu(&out_curr, block_size);
	*out_len = out_curr - out;
	
	gettimeofday(&end, NULL);

	// Allocate DPUs
	gettimeofday(&start, NULL);
	struct dpu_set_t dpus;
	struct dpu_set_t dpu_rank;
	struct dpu_set_t dpu;
	DPU_ASSERT(dpu_alloc(NR_DPUS, NULL, &dpus));
	gettimeofday(&end, NULL);

	// Load program
	gettimeofday(&start, NULL);
	DPU_ASSERT(dpu_load(dpus, DPU_COMPRESS_PROGRAM, NULL));
	gettimeofday(&end, NULL);

	// Copy variables common to all DPUs
	gettimeofday(&start, NULL);
#ifdef BULK_XFER
	DPU_ASSERT(dpu_prepare_xfer(dpus, &block_size));
       	DPU_ASSERT(dpu_push_xfer(dpus, DPU_XFER_TO_DPU, "block_size", 0, sizeof(uint32_t), DPU_XFER_DEFAULT));
#else
	DPU_ASSERT(dpu_copy_to(dpus, "block_size", 0, &block_size, sizeof(uint32_t)));
#endif

	dpu_idx = 0;
	DPU_RANK_FOREACH(dpus, dpu_rank) {
#ifdef BULK_XFER
		uint32_t largest_input_length = 0;
		uint32_t starting_dpu_idx = dpu_idx;
#endif
		DPU_FOREACH(dpu_rank, dpu) {
			// Add check to get rid of array out of bounds compiler warning
			if (dpu_idx >= NR_DPUS)
				break; 

			uint32_t input_length = 0;
			if ((dpu_idx != (NR_DPUS - 1)) && (input_block_offset[dpu_idx + 1][0] != 0)) {
				uint32_t blocks = (input_block_offset[dpu_idx + 1][0] - input_block_offset[dpu_idx][0]);
				input_length = blocks * block_size;
			}
			else if ((dpu_idx == 0) || (input_block_offset[dpu_idx][0] != 0)) {
				input_length = in_len - (input_block_offset[dpu_idx][0] * block_size);
			} 
			DPU_ASSERT(dpu_copy_to(dpu, "input_length", 0, &input_length, sizeof(uint32_t)));

#ifdef BULK_XFER		
			if (largest_input_length < input_length)
				largest_input_length = input_length;
		
			// If all prepared transfers have a larger transfer length, push them first
			// and then set up the next transfer
			if (input_length < largest_input_length) {
				DPU_ASSERT(dpu_push_xfer(dpu_rank, DPU_XFER_TO_DPU, "input_buffer", 0, ALIGN(largest_input_length, 8), DPU_XFER_DEFAULT));
				largest_input_length = input_length;
			}

			DPU_ASSERT(dpu_prepare_xfer(dpu, (void *)(in_curr + (input_block_offset[dpu_idx][0] * block_size))));	
#else
			DPU_ASSERT(dpu_copy_to(dpu, "input_block_offset", 0, input_block_offset[dpu_idx], sizeof(uint32_t) * NR_TASKLETS));
			DPU_ASSERT(dpu_copy_to(dpu, "output_offset", 0, output_offset[dpu_idx], sizeof(uint32_t) * NR_TASKLETS));
			DPU_ASSERT(dpu_copy_to(dpu, "input_buffer", 0, in_curr + (input_block_offset[dpu_idx][0] * block_size), ALIGN(input_length, 8)));
#endif
			dpu_idx++;
		}

#ifdef BULK_XFER
		DPU_ASSERT(dpu_push_xfer(dpu_rank, DPU_XFER_TO_DPU, "input_buffer", 0, ALIGN(largest_input_length, 8), DPU_XFER_DEFAULT));
		
		dpu_idx = starting_dpu_idx;
		DPU_FOREACH(dpu_rank, dpu) {
			DPU_ASSERT(dpu_prepare_xfer(dpu, (void *)input_block_offset[dpu_idx]));
			dpu_idx++;
		}
		DPU_ASSERT(dpu_push_xfer(dpu_rank, DPU_XFER_TO_DPU, "input_block_offset", 0, sizeof(uint32_t) * NR_TASKLETS, DPU_XFER_DEFAULT));

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

	// Open the output file and write the header
	FILE *fout = fopen("compressed.snappy", "w");
	fwrite(out, sizeof(uint8_t), *out_len, fout);
	
	// Deallocate the DPUs
	uint32_t max_output_length = snappy_max_compressed_length(input_blocks_per_dpu * block_size);
	dpu_idx = 0;
	DPU_RANK_FOREACH(dpus, dpu_rank) {
		uint32_t starting_dpu_idx = dpu_idx;
		gettimeofday(&start, NULL);

		// Get number of DPUs in this rank
		uint32_t nr_dpus;
		DPU_ASSERT(dpu_get_nr_dpus(dpu_rank, &nr_dpus));
		
		uint8_t *dpu_bufs[NR_DPUS] = {NULL};
		uint32_t output_length[NR_DPUS][NR_TASKLETS] = {0};

#ifdef BULK_XFER
		uint32_t largest_output_length = 0;
		DPU_FOREACH(dpu_rank, dpu) {
			DPU_ASSERT(dpu_prepare_xfer(dpu, output_length[dpu_idx]));
			dpu_idx++;
		}
		DPU_ASSERT(dpu_push_xfer(dpu_rank, DPU_XFER_FROM_DPU, "output_length", 0, sizeof(uint32_t) * NR_TASKLETS, DPU_XFER_DEFAULT));
		dpu_idx = starting_dpu_idx;
#endif

		DPU_FOREACH(dpu_rank, dpu) {
#ifndef BULK_XFER
			DPU_ASSERT(dpu_copy_from(dpu, "output_length", 0, output_length[dpu_idx], sizeof(uint32_t) * NR_TASKLETS));
#endif	
			// Calculate the total output length
			uint32_t dpu_output_length = 0;
			for (uint8_t i = 0; i < NR_TASKLETS; i++) {
				if (output_length[dpu_idx][i] != 0) {
					*out_len += output_length[dpu_idx][i];
					dpu_output_length = output_offset[dpu_idx][i] + output_length[dpu_idx][i];
				}
			}

			// Prepare the transfer
			dpu_bufs[dpu_idx] = malloc(max_output_length);
#ifdef BULK_XFER
			if (largest_output_length < dpu_output_length)
				largest_output_length = dpu_output_length;

			DPU_ASSERT(dpu_prepare_xfer(dpu, (void *)dpu_bufs[dpu_idx]));
#else
			DPU_ASSERT(dpu_copy_from(dpu, "output_buffer", 0, dpu_bufs[dpu_idx], ALIGN(dpu_output_length, 8)));
#endif

			dpu_idx++;
		}
		
#ifdef BULK_XFER
		DPU_ASSERT(dpu_push_xfer(dpu_rank, DPU_XFER_FROM_DPU, "output_buffer", 0, ALIGN(largest_output_length, 8), DPU_XFER_DEFAULT));
#endif
		// Don't count the time it takes to read the DPU log or write the data to a file, 
		// since we don't count that for the host
		gettimeofday(&end, NULL);

		// Print the logs
		dpu_idx = starting_dpu_idx;
		DPU_FOREACH(dpu_rank, dpu) {
			printf("------DPU %d Logs------\n", dpu_idx);
			DPU_ASSERT(dpu_log_read(dpu, stdout));
			dpu_idx++;
		}

		for (uint32_t d = nr_dpus; d > 0; d--) {
			uint32_t curr_dpu_idx = dpu_idx - d;
			for (uint8_t i = 0; i < NR_TASKLETS; i++) {
				fwrite(&dpu_bufs[curr_dpu_idx][output_offset[curr_dpu_idx][i]], sizeof(uint8_t), output_length[curr_dpu_idx][i], fout);
			}
			free(dpu_bufs[curr_dpu_idx]);
		}
	}

	printf("Compressed %ld bytes to: %s\n", *out_len, "test/alice_c.snappy");	
	printf("Compression ratio: %f\n", 1 - (double)*out_len / (double)in_len);

	gettimeofday(&start, NULL);
	DPU_ASSERT(dpu_free(dpus));
	gettimeofday(&end, NULL);

	fclose(fout);

	return SNAPPY_OK;
}
