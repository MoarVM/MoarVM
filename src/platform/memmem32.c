/*-
 * Original FreeBSD code Copyright (c) 2005-2014 Rich Felker, et al.
 * Modifications for MVM 32 bit search Copyright (c) 2018 Samantha McVey
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <string.h>
#include <stdint.h>
/* memmem_two_uint32 uses 64 bit integer reads extensively. Only use it on 64
 * bit processors since it will likely be slower on 32 bit processors. */
#include "moar.h"
#if 8 <= MVM_PTR_SIZE && defined(MVM_CAN_UNALIGNED_INT64)
#define USE_MEMMEM_TWO_UINT32 1
#endif

/* This is a modification of the memmem used in FreeBSD to allow us to quickly
 * search 32 bit strings. This is much more efficient than searching by byte
 * since we can skip every 4 bytes instead of every 1 byte, as well as the fact
 * that in a 32 bit integer, some of the bytes will be empty, making it even
 * less efficient.
 * The only caveats is the table uses a modulus so it can only jump to the next
 * codepoint of the same modulus. */

/* For memmem_one32 we just look for a single 32 bit integer in the haystack,
 * simple. */
static uint32_t * memmem_one_uint32(const uint32_t *h0, const uint32_t *n0, const uint32_t *end_h0) {
	uint32_t *h           = (uint32_t*)h0;
	const uint32_t  n     = *n0;
	const uint32_t *end_h = end_h0 - 1;
	for (; h <= end_h; h++) {
		if (*h == n) return h;
	}
	return NULL;
}
/* For memmem_two_uint32 we work similarly to memmem_one_uint32 except we read
 * the needle of length 2 as a 64 bit integer, progressing along the haystack
 * 32 bits at a time, and comparing the 64 bit needle to the haystack's pointer
 * casted as a 64 bit integer. */
#if defined(USE_MEMMEM_TWO_UINT32)
static uint32_t * memmem_two_uint32(const uint32_t *h0, const uint32_t *n0, const uint32_t *end_h0) {
	uint32_t *h           = (uint32_t*)h0;
	const uint64_t  n     = *((uint64_t*)n0);
	const uint32_t *end_h = end_h0 - 2;
	for (; h <= end_h; h++) {
		if (*((uint64_t*)h) == n) return h;
	}
	return NULL;
}
#endif

#define MVM_MAX(a,b) ((a)>(b)?(a):(b))
#define MVM_MIN(a,b) ((a)<(b)?(a):(b))

#define BITOP(a,b,op) \
 ((a)[(size_t)(b)/(8*sizeof *(a))] op (size_t)1<<((size_t)(b)%(8*sizeof *(a))))

/* Original FreeBSD comment:
 * Two Way string search algorithm, with a bad shift table applied to the last
 * byte of the window. A bit array marks which entries in the shift table are
 * initialized to avoid fully initializing a 1kb/2kb table.
 *
 * Reference: CROCHEMORE M., PERRIN D., 1991, Two-way string-matching,
 * Journal of the ACM 38(3):651-675
 */

/* The modulus number is the length of the shift table. We use this to quickly
 * lookup a codepoint and see how much we can shift forward. Caveat: if two numbers
 * have the same modulus we can only shift as much forward as much as a codepoint
 * with that modulus. */
#define MODNUMBER 256
#define MOD_OP(a) ((a) % MODNUMBER)
static char *twoway_memmem_uint32(const uint32_t *h, const uint32_t *z, const uint32_t *n, size_t l)
{
	size_t i, ip, jp, k, p, ms, p0, mem, mem0;
	size_t byteset[32 / sizeof(size_t)] = { 0 };
	uint32_t shift[MODNUMBER];

	/* Computing length of needle and fill shift table */
	for (i=0; i<l; i++) {
		BITOP(byteset, MOD_OP(n[i]), |=);
		shift[MOD_OP(n[i])] = i+1;
	}

	/* Compute maximal suffix */
	ip = -1; jp = 0; k = p = 1;
	while (jp+k<l) {
		if (n[ip+k] == n[jp+k]) {
			if (k == p) {
				jp += p;
				k = 1;
			} else k++;
		} else if (n[ip+k] > n[jp+k]) {
			jp += k;
			k = 1;
			p = jp - ip;
		} else {
			ip = jp++;
			k = p = 1;
		}
	}
	ms = ip;
	p0 = p;

	/* And with the opposite comparison */
	ip = -1; jp = 0; k = p = 1;
	while (jp+k<l) {
		if (n[ip+k] == n[jp+k]) {
			if (k == p) {
				jp += p;
				k = 1;
			} else k++;
		} else if (n[ip+k] < n[jp+k]) {
			jp += k;
			k = 1;
			p = jp - ip;
		} else {
			ip = jp++;
			k = p = 1;
		}
	}
	if (ip+1 > ms+1) ms = ip;
	else p = p0;

	/* Periodic needle? */
	if (memcmp(n, n+p, ms+1)) {
		mem0 = 0;
		p = MVM_MAX(ms, l-ms-1) + 1;
	} else mem0 = l-p;
	mem = 0;

	/* Search loop */
	for (;;) {
		/* If remainder of haystack is shorter than needle, done */
		if ((size_t)(z-h) < l) {
			return 0;
		}

		/* Check last byte first; advance by shift on mismatch */
		if (BITOP(byteset, MOD_OP(h[l-1]), &)) {
			k = l-shift[MOD_OP(h[l-1])];
			if (k) {
				if (mem0 && mem && k < p) k = l-p;
				h += k;
				mem = 0;
				continue;
			}
		} else {
			h += l;
			mem = 0;
			continue;
		}

		/* Compare right half */
		for (k=MVM_MAX(ms+1,mem); k<l && n[k] == h[k]; k++);
		if (k < l) {
			h += k-ms;
			mem = 0;
			continue;
		}
		/* Compare left half */
		for (k=ms+1; k>mem && n[k-1] == h[k-1]; k--);
		if (k <= mem) {
			return (char *)h;
		}
		h += p;
		mem = mem0;
	}
}
/* Finds the memory location of the needle in the haystack. Arguments are the
 * memory location of the start of the Haystack, the memory location of the start
 * of the needle and the needle length as well as the Haystack length.
 * H_len and n_len are measured the size of a uint32_t. */
void * memmem_uint32(const void *h0, size_t H_len, const void *n0, size_t n_len)
{
	const uint32_t *h = (uint32_t*)h0,
	               *n = (uint32_t*)n0;

	/* The empty string can be found at the start of any string. */
	if (!n_len) return (void *)h;

	/* Return immediately when needle is longer than haystack. */
	if (H_len < n_len)
		return NULL;

#if defined(USE_MEMMEM_TWO_UINT32)
	if (n_len == 1) {
		return memmem_one_uint32(h, n, h+H_len);
	}
	/* Otherwise do a search for the first two uint32_t integers at the start
	 * of the needle. */
	else {
		h = memmem_two_uint32(h, n, h+H_len);
	}
	/* If nothing found or if we have a needle of length 2, we already have
	 * our result. */
	if (!h || n_len == 2)
		return (void *)h;
#else
	/* With needle length 1 use fast search for only one uint32_t. */
	h = memmem_one_uint32(h, n, h+H_len);
	if (!h || n_len == 1)
		return (void *)h;
#endif
	/* Since our Haystack (may) have been moved forward to the first match
	 * of the start of the needle, also reduce the Haystack's length to match. */
	H_len -= h - (uint32_t*)h0;
	/* No more work to do if the needle is longer than where we are now. */
	if (H_len < n_len)
		return NULL;

	return twoway_memmem_uint32(h, h+H_len, n, n_len);
}
