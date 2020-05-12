/*
 * C port of the snappy compressor from Google.
 * This is a very fast compressor with comparable compression to lzo.
 * Works best on 64bit little-endian, but should be good on others too.
 * Ported by Andi Kleen.
 * Uptodate with snappy 1.1.0
 */

/*
 * Copyright 2005 Google Inc. All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef __KERNEL__
#include <linux/kernel.h>
#ifdef SG
#include <linux/uio.h>
#endif
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/snappy.h>
#include <linux/vmalloc.h>
#include <asm/unaligned.h>
#else
#include "snappy.h"
#include "compat.h"
#endif

#define CRASH_UNLESS(x) BUG_ON(!(x))
#define CHECK(cond) CRASH_UNLESS(cond)
#define CHECK_LE(a, b) CRASH_UNLESS((a) <= (b))
#define CHECK_GE(a, b) CRASH_UNLESS((a) >= (b))
#define CHECK_EQ(a, b) CRASH_UNLESS((a) == (b))
#define CHECK_NE(a, b) CRASH_UNLESS((a) != (b))
#define CHECK_LT(a, b) CRASH_UNLESS((a) < (b))
#define CHECK_GT(a, b) CRASH_UNLESS((a) > (b))

#define UNALIGNED_LOAD16(_p) get_unaligned((u16 *)(_p))
#define UNALIGNED_LOAD32(_p) get_unaligned((u32 *)(_p))
#define UNALIGNED_LOAD64(_p) get_unaligned64((u64 *)(_p))

#define UNALIGNED_STORE16(_p, _val) put_unaligned(_val, (u16 *)(_p))
#define UNALIGNED_STORE32(_p, _val) put_unaligned(_val, (u32 *)(_p))
#define UNALIGNED_STORE64(_p, _val) put_unaligned64(_val, (u64 *)(_p))

/*
 * This can be more efficient than UNALIGNED_LOAD64 + UNALIGNED_STORE64
 * on some platforms, in particular ARM.
 */
static inline void unaligned_copy64(const void *src, void *dst)
{
	if (sizeof(void *) == 8) {
		UNALIGNED_STORE64(dst, UNALIGNED_LOAD64(src));
	} else {
		const char *src_char = (const char *)(src);
		char *dst_char = (char *)(dst);

		UNALIGNED_STORE32(dst_char, UNALIGNED_LOAD32(src_char));
		UNALIGNED_STORE32(dst_char + 4, UNALIGNED_LOAD32(src_char + 4));
	}
}

#ifdef NDEBUG

#define DCHECK(cond) do {} while(0)
#define DCHECK_LE(a, b) do {} while(0)
#define DCHECK_GE(a, b) do {} while(0)
#define DCHECK_EQ(a, b) do {} while(0)
#define DCHECK_NE(a, b) do {} while(0)
#define DCHECK_LT(a, b) do {} while(0)
#define DCHECK_GT(a, b) do {} while(0)

#else

#define DCHECK(cond) CHECK(cond)
#define DCHECK_LE(a, b) CHECK_LE(a, b)
#define DCHECK_GE(a, b) CHECK_GE(a, b)
#define DCHECK_EQ(a, b) CHECK_EQ(a, b)
#define DCHECK_NE(a, b) CHECK_NE(a, b)
#define DCHECK_LT(a, b) CHECK_LT(a, b)
#define DCHECK_GT(a, b) CHECK_GT(a, b)

#endif

static inline bool is_little_endian(void)
{
#ifdef __LITTLE_ENDIAN__
	return true;
#endif
	return false;
}

static inline int log2_floor(u32 n)
{
	return n == 0 ? -1 : 31 ^ __builtin_clz(n);
}

static inline int find_lsb_set_non_zero(u32 n)
{
	return __builtin_ctz(n);
}

static inline int find_lsb_set_non_zero64(u64 n)
{
	return __builtin_ctzll(n);
}

#define kmax32 5

/*
 * REQUIRES   "ptr" points to a buffer of length sufficient to hold "v".
 *  EFFECTS    Encodes "v" into "ptr" and returns a pointer to the
 *            byte just past the last encoded byte.
 */
static inline char *varint_encode32(char *sptr, u32 v)
{
	/* Operate on characters as unsigneds */
	unsigned char *ptr = (unsigned char *)(sptr);
	static const int B = 128;

	if (v < (1 << 7)) {
		*(ptr++) = v;
	} else if (v < (1 << 14)) {
		*(ptr++) = v | B;
		*(ptr++) = v >> 7;
	} else if (v < (1 << 21)) {
		*(ptr++) = v | B;
		*(ptr++) = (v >> 7) | B;
		*(ptr++) = v >> 14;
	} else if (v < (1 << 28)) {
		*(ptr++) = v | B;
		*(ptr++) = (v >> 7) | B;
		*(ptr++) = (v >> 14) | B;
		*(ptr++) = v >> 21;
	} else {
		*(ptr++) = v | B;
		*(ptr++) = (v >> 7) | B;
		*(ptr++) = (v >> 14) | B;
		*(ptr++) = (v >> 21) | B;
		*(ptr++) = v >> 28;
	}
	return (char *)(ptr);
}

#ifdef SG

static inline void *n_bytes_after_addr(void *addr, size_t n_bytes)
{
    return (void *) ((char *)addr + n_bytes);
}

struct source {
	struct iovec *iov;
	int iovlen;
	int curvec;
	int curoff;
	size_t total;
};

/* Only valid at beginning when nothing is consumed */
static inline int available(struct source *s)
{
	return s->total;
}

static inline const char *peek(struct source *s, size_t *len)
{
	if (likely(s->curvec < s->iovlen)) {
		struct iovec *iv = &s->iov[s->curvec];
		if (s->curoff < iv->iov_len) { 
			*len = iv->iov_len - s->curoff;
			return n_bytes_after_addr(iv->iov_base, s->curoff);
		}
	}
	*len = 0;
	return NULL;
}

static inline void skip(struct source *s, size_t n)
{
	struct iovec *iv = &s->iov[s->curvec];
	s->curoff += n;
	DCHECK_LE(s->curoff, iv->iov_len);
	if (s->curoff >= iv->iov_len && s->curvec + 1 < s->iovlen) {
		s->curoff = 0;
		s->curvec++;
	}
}

struct sink {
	struct iovec *iov;
	int iovlen;
	unsigned curvec;
	unsigned curoff;
	unsigned written;
};

static inline void append(struct sink *s, const char *data, size_t n)
{
	struct iovec *iov = &s->iov[s->curvec];
	char *dst = n_bytes_after_addr(iov->iov_base, s->curoff);
	size_t nlen = min_t(size_t, iov->iov_len - s->curoff, n);
	if (data != dst)
		memcpy(dst, data, nlen);
	s->written += n;
	s->curoff += nlen;
	while ((n -= nlen) > 0) {
		data += nlen;
		s->curvec++;
		DCHECK_LT(s->curvec, s->iovlen);
		iov++;
		nlen = min_t(size_t, iov->iov_len, n);
		memcpy(iov->iov_base, data, nlen);
		s->curoff = nlen;
	}
}

static inline void *sink_peek(struct sink *s, size_t n)
{
	struct iovec *iov = &s->iov[s->curvec];
	if (s->curvec < iov->iov_len && iov->iov_len - s->curoff >= n)
		return n_bytes_after_addr(iov->iov_base, s->curoff);
	return NULL;
}

#else

struct source {
	const char *ptr;
	size_t left;
};

static inline int available(struct source *s)
{
	return s->left;
}

static inline const char *peek(struct source *s, size_t * len)
{
	*len = s->left;
	return s->ptr;
}

static inline void skip(struct source *s, size_t n)
{
	s->left -= n;
	s->ptr += n;
}

struct sink {
	char *dest;
};

static inline void append(struct sink *s, const char *data, size_t n)
{
	if (data != s->dest)
		memcpy(s->dest, data, n);
	s->dest += n;
}

#define sink_peek(s, n) sink_peek_no_sg(s)

static inline void *sink_peek_no_sg(const struct sink *s)
{
	return s->dest;
}

#endif

/*
 * Any hash function will produce a valid compressed bitstream, but a good
 * hash function reduces the number of collisions and thus yields better
 * compression for compressible input, and more speed for incompressible
 * input. Of course, it doesn't hurt if the hash function is reasonably fast
 * either, as it gets called a lot.
 */
static inline u32 hash_bytes(u32 bytes, int shift)
{
	u32 kmul = 0x1e35a7bd;
	return (bytes * kmul) >> shift;
}

static inline u32 hash(const char *p, int shift)
{
	return hash_bytes(UNALIGNED_LOAD32(p), shift);
}

/*
 * Compressed data can be defined as:
 *    compressed := item* literal*
 *    item       := literal* copy
 *
 * The trailing literal sequence has a space blowup of at most 62/60
 * since a literal of length 60 needs one tag byte + one extra byte
 * for length information.
 *
 * Item blowup is trickier to measure.  Suppose the "copy" op copies
 * 4 bytes of data.  Because of a special check in the encoding code,
 * we produce a 4-byte copy only if the offset is < 65536.  Therefore
 * the copy op takes 3 bytes to encode, and this type of item leads
 * to at most the 62/60 blowup for representing literals.
 *
 * Suppose the "copy" op copies 5 bytes of data.  If the offset is big
 * enough, it will take 5 bytes to encode the copy op.  Therefore the
 * worst case here is a one-byte literal followed by a five-byte copy.
 * I.e., 6 bytes of input turn into 7 bytes of "compressed" data.
 *
 * This last factor dominates the blowup, so the final estimate is:
 */
size_t snappy_max_compressed_length(size_t source_len)
{
	return 32 + source_len + source_len / 6;
}
EXPORT_SYMBOL(snappy_max_compressed_length);

enum {
	LITERAL = 0,
	COPY_1_BYTE_OFFSET = 1,	/* 3 bit length + 3 bits of offset in opcode */
	COPY_2_BYTE_OFFSET = 2,
	COPY_4_BYTE_OFFSET = 3
};

static inline char *emit_literal(char *op,
				 const char *literal,
				 int len, bool allow_fast_path)
{
	int n = len - 1;	/* Zero-length literals are disallowed */

	if (n < 60) {
		/* Fits in tag byte */
		*op++ = LITERAL | (n << 2);

/*
 * The vast majority of copies are below 16 bytes, for which a
 * call to memcpy is overkill. This fast path can sometimes
 * copy up to 15 bytes too much, but that is okay in the
 * main loop, since we have a bit to go on for both sides:
 *
 *   - The input will always have kInputMarginBytes = 15 extra
 *     available bytes, as long as we're in the main loop, and
 *     if not, allow_fast_path = false.
 *   - The output will always have 32 spare bytes (see
 *     MaxCompressedLength).
 */
		if (allow_fast_path && len <= 16) {
			unaligned_copy64(literal, op);
			unaligned_copy64(literal + 8, op + 8);
			return op + len;
		}
	} else {
		/* Encode in upcoming bytes */
		char *base = op;
		int count = 0;
		op++;
		while (n > 0) {
			*op++ = n & 0xff;
			n >>= 8;
			count++;
		}
		DCHECK(count >= 1);
		DCHECK(count <= 4);
		*base = LITERAL | ((59 + count) << 2);
	}
	memcpy(op, literal, len);
	return op + len;
}

static inline char *emit_copy_less_than64(char *op, int offset, int len)
{
	DCHECK_LE(len, 64);
	DCHECK_GE(len, 4);
	DCHECK_LT(offset, 65536);

	if ((len < 12) && (offset < 2048)) {
		int len_minus_4 = len - 4;
		DCHECK(len_minus_4 < 8);	/* Must fit in 3 bits */
		*op++ =
		    COPY_1_BYTE_OFFSET + ((len_minus_4) << 2) + ((offset >> 8)
								 << 5);
		*op++ = offset & 0xff;
	} else {
		*op++ = COPY_2_BYTE_OFFSET + ((len - 1) << 2);
		put_unaligned_le16(offset, op);
		op += 2;
	}
	return op;
}

static inline char *emit_copy(char *op, int offset, int len)
{
	/*
	 * Emit 64 byte copies but make sure to keep at least four bytes
	 * reserved
	 */
	while (len >= 68) {
		op = emit_copy_less_than64(op, offset, 64);
		len -= 64;
	}

	/*
	 * Emit an extra 60 byte copy if have too much data to fit in
	 * one copy
	 */
	if (len > 64) {
		op = emit_copy_less_than64(op, offset, 60);
		len -= 60;
	}

	/* Emit remainder */
	op = emit_copy_less_than64(op, offset, len);
	return op;
}

/*
 * The size of a compression block. Note that many parts of the compression
 * code assumes that kBlockSize <= 65536; in particular, the hash table
 * can only store 16-bit offsets, and EmitCopy() also assumes the offset
 * is 65535 bytes or less. Note also that if you change this, it will
 * affect the framing format
 * Note that there might be older data around that is compressed with larger
 * block sizes, so the decompression code should not rely on the
 * non-existence of long backreferences.
 */
#define kblock_log 16
#define kblock_size (1 << kblock_log)

/* 
 * This value could be halfed or quartered to save memory 
 * at the cost of slightly worse compression.
 */
#define kmax_hash_table_bits 14
#define kmax_hash_table_size (1U << kmax_hash_table_bits)

/*
 * Use smaller hash table when input.size() is smaller, since we
 * fill the table, incurring O(hash table size) overhead for
 * compression, and if the input is short, we won't need that
 * many hash table entries anyway.
 */
static u16 *get_hash_table(struct snappy_env *env, size_t input_size,
			      int *table_size)
{
	unsigned htsize = 256;

	DCHECK(kmax_hash_table_size >= 256);
	while (htsize < kmax_hash_table_size && htsize < input_size)
		htsize <<= 1;
	CHECK_EQ(0, htsize & (htsize - 1));
	CHECK_LE(htsize, kmax_hash_table_size);

	u16 *table;
	table = env->hash_table;

	*table_size = htsize;
	memset(table, 0, htsize * sizeof(*table));
	return table;
}

/*
 * Return the largest n such that
 *
 *   s1[0,n-1] == s2[0,n-1]
 *   and n <= (s2_limit - s2).
 *
 * Does not read *s2_limit or beyond.
 * Does not read *(s1 + (s2_limit - s2)) or beyond.
 * Requires that s2_limit >= s2.
 *
 * Separate implementation for x86_64, for speed.  Uses the fact that
 * x86_64 is little endian.
 */
#if defined(__LITTLE_ENDIAN__) && BITS_PER_LONG == 64
static inline int find_match_length(const char *s1,
				    const char *s2, const char *s2_limit)
{
	int matched = 0;

	DCHECK_GE(s2_limit, s2);
	/*
	 * Find out how long the match is. We loop over the data 64 bits at a
	 * time until we find a 64-bit block that doesn't match; then we find
	 * the first non-matching bit and use that to calculate the total
	 * length of the match.
	 */
	while (likely(s2 <= s2_limit - 8)) {
		if (unlikely
		    (UNALIGNED_LOAD64(s2) == UNALIGNED_LOAD64(s1 + matched))) {
			s2 += 8;
			matched += 8;
		} else {
			/*
			 * On current (mid-2008) Opteron models there
			 * is a 3% more efficient code sequence to
			 * find the first non-matching byte.  However,
			 * what follows is ~10% better on Intel Core 2
			 * and newer, and we expect AMD's bsf
			 * instruction to improve.
			 */
			u64 x =
			    UNALIGNED_LOAD64(s2) ^ UNALIGNED_LOAD64(s1 +
								    matched);
			int matching_bits = find_lsb_set_non_zero64(x);
			matched += matching_bits >> 3;
			return matched;
		}
	}
	while (likely(s2 < s2_limit)) {
		if (likely(s1[matched] == *s2)) {
			++s2;
			++matched;
		} else {
			return matched;
		}
	}
	return matched;
}
#else
static inline int find_match_length(const char *s1,
				    const char *s2, const char *s2_limit)
{
	/* Implementation based on the x86-64 version, above. */
	DCHECK_GE(s2_limit, s2);
	int matched = 0;

	while (s2 <= s2_limit - 4 &&
	       UNALIGNED_LOAD32(s2) == UNALIGNED_LOAD32(s1 + matched)) {
		s2 += 4;
		matched += 4;
	}
	if (is_little_endian() && s2 <= s2_limit - 4) {
		u32 x =
		    UNALIGNED_LOAD32(s2) ^ UNALIGNED_LOAD32(s1 + matched);
		int matching_bits = find_lsb_set_non_zero(x);
		matched += matching_bits >> 3;
	} else {
		while ((s2 < s2_limit) && (s1[matched] == *s2)) {
			++s2;
			++matched;
		}
	}
	return matched;
}
#endif

/*
 * For 0 <= offset <= 4, GetU32AtOffset(GetEightBytesAt(p), offset) will
 *  equal UNALIGNED_LOAD32(p + offset).  Motivation: On x86-64 hardware we have
 * empirically found that overlapping loads such as
 *  UNALIGNED_LOAD32(p) ... UNALIGNED_LOAD32(p+1) ... UNALIGNED_LOAD32(p+2)
 * are slower than UNALIGNED_LOAD64(p) followed by shifts and casts to u32.
 *
 * We have different versions for 64- and 32-bit; ideally we would avoid the
 * two functions and just inline the UNALIGNED_LOAD64 call into
 * GetUint32AtOffset, but GCC (at least not as of 4.6) is seemingly not clever
 * enough to avoid loading the value multiple times then. For 64-bit, the load
 * is done when GetEightBytesAt() is called, whereas for 32-bit, the load is
 * done at GetUint32AtOffset() time.
 */

#if BITS_PER_LONG == 64

typedef u64 eight_bytes_reference;

static inline eight_bytes_reference get_eight_bytes_at(const char* ptr)
{
	return UNALIGNED_LOAD64(ptr);
}

static inline u32 get_u32_at_offset(u64 v, int offset)
{
	DCHECK_GE(offset, 0);
	DCHECK_LE(offset, 4);
	return v >> (is_little_endian()? 8 * offset : 32 - 8 * offset);
}

#else

typedef const char *eight_bytes_reference;

static inline eight_bytes_reference get_eight_bytes_at(const char* ptr) 
{
	return ptr;
}

static inline u32 get_u32_at_offset(const char *v, int offset) 
{
	DCHECK_GE(offset, 0);
	DCHECK_LE(offset, 4);
	return UNALIGNED_LOAD32(v + offset);
}
#endif

/*
 * Flat array compression that does not emit the "uncompressed length"
 *  prefix. Compresses "input" string to the "*op" buffer.
 *
 * REQUIRES: "input" is at most "kBlockSize" bytes long.
 * REQUIRES: "op" points to an array of memory that is at least
 * "MaxCompressedLength(input.size())" in size.
 * REQUIRES: All elements in "table[0..table_size-1]" are initialized to zero.
 * REQUIRES: "table_size" is a power of two
 *
 * Returns an "end" pointer into "op" buffer.
 * "end - op" is the compressed size of "input".
 */

static char *compress_fragment(const char *const input,
			       const size_t input_size,
			       char *op, u16 * table, const unsigned table_size)
{
	/* "ip" is the input pointer, and "op" is the output pointer. */
	const char *ip = input;
	CHECK_EQ(table_size & (table_size - 1), 0);
	const int shift = 32 - log2_floor(table_size);
	DCHECK_EQ(UINT_MAX >> shift, table_size - 1);
	const char *ip_end = input + input_size;
	const char *baseip = ip;
	/*
	 * Bytes in [next_emit, ip) will be emitted as literal bytes.  Or
	 *  [next_emit, ip_end) after the main loop.
	 */
	const char *next_emit = ip;

	const unsigned kinput_margin_bytes = 15;

	if (likely(input_size >= kinput_margin_bytes)) {
		const char *const ip_limit = input + input_size -
			kinput_margin_bytes;

		u32 next_hash;
		for (next_hash = hash(++ip, shift);;) {
			DCHECK_LT(next_emit, ip);
/*
 * The body of this loop calls EmitLiteral once and then EmitCopy one or
 * more times.  (The exception is that when we're close to exhausting
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
			u32 skip_bytes = 32;

			const char *next_ip = ip;
			const char *candidate;
			do {
				ip = next_ip;
				u32 hval = next_hash;
				DCHECK_EQ(hval, hash(ip, shift));
				u32 bytes_between_hash_lookups = skip_bytes++ >> 5;
				next_ip = ip + bytes_between_hash_lookups;
				if (unlikely(next_ip > ip_limit)) {
					goto emit_remainder;
				}
				next_hash = hash(next_ip, shift);
				candidate = baseip + table[hval];
				DCHECK_GE(candidate, baseip);
				DCHECK_LT(candidate, ip);
				table[hval] = ip - baseip;
			} while (likely(UNALIGNED_LOAD32(ip) !=
					UNALIGNED_LOAD32(candidate)));

/*
 * Step 2: A 4-byte match has been found.  We'll later see if more
 * than 4 bytes match.  But, prior to the match, input
 * bytes [next_emit, ip) are unmatched.  Emit them as "literal bytes."
 */
			DCHECK_LE(next_emit + 16, ip_end);
			op = emit_literal(op, next_emit, ip - next_emit, true);

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
			eight_bytes_reference input_bytes;
			u32 candidate_bytes = 0;

			do {
/*
 * We have a 4-byte match at ip, and no need to emit any
 *  "literal bytes" prior to ip.
 */
				const char *base = ip;
				int matched = 4 +
				    find_match_length(candidate + 4, ip + 4,
						      ip_end);
				ip += matched;
				int offset = base - candidate;
				DCHECK_EQ(0, memcmp(base, candidate, matched));
				op = emit_copy(op, offset, matched);
/*
 * We could immediately start working at ip now, but to improve
 * compression we first update table[Hash(ip - 1, ...)].
 */
				const char *insert_tail = ip - 1;
				next_emit = ip;
				if (unlikely(ip >= ip_limit)) {
					goto emit_remainder;
				}
				input_bytes = get_eight_bytes_at(insert_tail);
				u32 prev_hash =
				    hash_bytes(get_u32_at_offset
					       (input_bytes, 0), shift);
				table[prev_hash] = ip - baseip - 1;
				u32 cur_hash =
				    hash_bytes(get_u32_at_offset
					       (input_bytes, 1), shift);
				candidate = baseip + table[cur_hash];
				candidate_bytes = UNALIGNED_LOAD32(candidate);
				table[cur_hash] = ip - baseip;
			} while (get_u32_at_offset(input_bytes, 1) ==
				 candidate_bytes);

			next_hash =
			    hash_bytes(get_u32_at_offset(input_bytes, 2),
				       shift);
			++ip;
		}
	}

emit_remainder:
	/* Emit the remaining bytes as a literal */
	if (next_emit < ip_end)
		op = emit_literal(op, next_emit, ip_end - next_emit, false);

	return op;
}

static inline int compress(struct snappy_env *env, struct source *reader,
			   struct sink *writer, size_t block_size)
{
	int err;
	size_t written = 0;
	int N = available(reader);

	while (N > 0) {
		/* Get next block to compress (without copying if possible) */
		size_t fragment_size;
		const char *fragment = peek(reader, &fragment_size);
		if (fragment_size == 0) {
			err = -EIO;
			goto out;
		}
		const unsigned num_to_read = min_t(int, N, block_size);
		size_t bytes_read = fragment_size;
        int pending_advance = 0;
		if (bytes_read >= num_to_read) {
			/* Buffer returned by reader is large enough */
			pending_advance = num_to_read;
			fragment_size = num_to_read;
		}
		else {
			memcpy(env->scratch, fragment, bytes_read);
			skip(reader, bytes_read);

			while (bytes_read < num_to_read) {
				fragment = peek(reader, &fragment_size);
				size_t n =
				    min_t(size_t, fragment_size,
					  num_to_read - bytes_read);
				memcpy((char *)(env->scratch) + bytes_read, fragment, n);
				bytes_read += n;
				skip(reader, n);
			}
			DCHECK_EQ(bytes_read, num_to_read);
			fragment = env->scratch;
			fragment_size = num_to_read;
		}
		if (fragment_size < num_to_read)
			return -EIO;

		/* Get encoding table for compression */
		int table_size;
		u16 *table = get_hash_table(env, num_to_read, &table_size);

		/* Compress input_fragment */
		char *dest;
		dest = sink_peek(writer, snappy_max_compressed_length(num_to_read));
		if (!dest) {
			/*
			 * Need a scratch buffer for the output,
			 * because the byte sink doesn't have enough
			 * in one piece.
			 */
			dest = env->scratch_output;
		}
		char *end = compress_fragment(fragment, fragment_size,
					      dest, table, table_size);

        /* Append the size of the compressed data */
        char clength[kmax32];
        char *p = varint_encode32(clength, end - dest);
        append(writer, clength, p - clength);
    	written += (p - clength);

        /* Append the size of the decompressed data */
        p = varint_encode32(clength, num_to_read);
        append(writer, clength, p - clength);
        written += (p - clength);

        /* Append the compressed data */
		append(writer, dest, end - dest);
		written += (end - dest);

		N -= num_to_read;
		skip(reader, pending_advance);
	}

	err = 0;
out:
	return err;
}

#ifdef SG

int snappy_compress_iov(struct snappy_env *env,
			struct iovec *iov_in,
			int iov_in_len,
			size_t input_length,
			struct iovec *iov_out,
			int *iov_out_len,
            size_t block_size,
			size_t *compressed_length)
{
	struct source reader = {
		.iov = iov_in,
		.iovlen = iov_in_len,
		.total = input_length
	};
	struct sink writer = {
		.iov = iov_out,
		.iovlen = *iov_out_len,
	};
	int err = compress(env, &reader, &writer, block_size);

	*iov_out_len = writer.curvec + 1;

	/* Compute how many bytes were added */
	*compressed_length = writer.written;
	return err;
}
EXPORT_SYMBOL(snappy_compress_iov);

/**
 * snappy_compress - Compress a buffer using the snappy compressor.
 * @env: Preallocated environment
 * @input: Input buffer
 * @input_length: Length of input_buffer
 * @block_size: Maximum decompressed block size that is compressed at a time
 * @compressed: Output buffer for compressed data
 * @compressed_length: The real length of the output written here.
 *
 * Return 0 on success, otherwise an negative error code.
 *
 * The output buffer must be at least
 * snappy_max_compressed_length(input_length) bytes long.
 *
 * Requires a preallocated environment from snappy_init_env.
 * The environment does not keep state over individual calls
 * of this function, just preallocates the memory.
 */
int snappy_compress(struct snappy_env *env,
		    const char *input,
		    size_t input_length, size_t block_size,
		    char *compressed, size_t *compressed_length)
{
	struct iovec iov_in = {
		.iov_base = (char *)input,
		.iov_len = input_length,
	};
	struct iovec iov_out = {
		.iov_base = compressed,
		.iov_len = 0xffffffff,
	};
	int out = 1;
	return snappy_compress_iov(env, 
				   &iov_in, 1, input_length, 
				   &iov_out, &out, block_size, compressed_length);
}
EXPORT_SYMBOL(snappy_compress);

#else
/**
 * snappy_compress - Compress a buffer using the snappy compressor.
 * @env: Preallocated environment
 * @input: Input buffer
 * @input_length: Length of input_buffer
 * @block_size: Maximum decompressed block size that is compressed at a time
 * @compressed: Output buffer for compressed data
 * @compressed_length: The real length of the output written here.
 *
 * Return 0 on success, otherwise an negative error code.
 *
 * The output buffer must be at least
 * snappy_max_compressed_length(input_length) bytes long.
 *
 * Requires a preallocated environment from snappy_init_env.
 * The environment does not keep state over individual calls
 * of this function, just preallocates the memory.
 */
int snappy_compress(struct snappy_env *env,
		    const char *input,
		    size_t input_length, size_t block_size,
		    char *compressed, size_t *compressed_length)
{
	struct source reader = {
		.ptr = input,
		.left = input_length
	};
	struct sink writer = {
		.dest = compressed,
	};
	int err = compress(env, &reader, &writer, block_size);

	/* Compute how many bytes were added */
	*compressed_length = (writer.dest - compressed);
	return err;
}
EXPORT_SYMBOL(snappy_compress);
#endif

static inline void clear_env(struct snappy_env *env)
{
    memset(env, 0, sizeof(*env));
}

#ifdef SG
/**
 * snappy_init_env_sg - Allocate snappy compression environment
 * @env: Environment to preallocate
 * @sg: Input environment ever does scather gather
 *
 * If false is passed to sg then multiple entries in an iovec
 * are not legal.
 * Returns 0 on success, otherwise negative errno.
 * Must run in process context.
 */
int snappy_init_env_sg(struct snappy_env *env, bool sg)
{
	if (snappy_init_env(env) < 0)
		goto error;

	if (sg) {
		env->scratch = vmalloc(kblock_size);
		if (!env->scratch)
			goto error;
		env->scratch_output =
			vmalloc(snappy_max_compressed_length(kblock_size));
		if (!env->scratch_output)
			goto error;
	}
	return 0;
error:
	snappy_free_env(env);
	return -ENOMEM;
}
EXPORT_SYMBOL(snappy_init_env_sg);
#endif

/**
 * snappy_init_env - Allocate snappy compression environment
 * @env: Environment to preallocate
 *
 * Passing multiple entries in an iovec is not allowed
 * on the environment allocated here.
 * Returns 0 on success, otherwise negative errno.
 * Must run in process context.
 */
int snappy_init_env(struct snappy_env *env)
{
    clear_env(env);
	env->hash_table = vmalloc(sizeof(u16) * kmax_hash_table_size);
	if (!env->hash_table)
		return -ENOMEM;
	return 0;
}
EXPORT_SYMBOL(snappy_init_env);

/**
 * snappy_free_env - Free an snappy compression environment
 * @env: Environment to free.
 *
 * Must run in process context.
 */
void snappy_free_env(struct snappy_env *env)
{
	vfree(env->hash_table);
#ifdef SG
	vfree(env->scratch);
	vfree(env->scratch_output);
#endif
	clear_env(env);
}
EXPORT_SYMBOL(snappy_free_env);