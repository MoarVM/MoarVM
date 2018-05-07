/*-
 * Original FreeBSD code Copyright (c) 2005-2014 Rich Felker, et al.
 * Modifications for 32 bit search Copyright (c) 2018 Samantha McVey
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
/*#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");*/

#include <string.h>
#include <stdint.h>
/* This is a modification of the memmem used in FreeBSD to allow us to quickly
 * search 32 bit strings. This is much more efficient than searching by byte
 * since we can skip every 4 bytes instead of every 1 byte, as well as the fact
 * that in a 32 bit integer, some of the bytes will be empty, making it even
 * less efficient.
 * The only caveats is the table uses a modulus so it can only jump to the next
 * codepoint of the same modulus. */

static uint32_t * memmem_one_uint32(const uint32_t *h0, const uint32_t n, const uint32_t *end_h) {
	uint32_t *h = (uint32_t*)h0;
	for (; h < end_h; h++) {
		if (*h == n) return h;
	}
	return NULL;
}
#define MAX(a,b) ((a)>(b)?(a):(b))
#define MIN(a,b) ((a)<(b)?(a):(b))

#define BITOP(a,b,op) \
 ((a)[(size_t)(b)/(8*sizeof *(a))] op (size_t)1<<((size_t)(b)%(8*sizeof *(a))))

/*
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
		p = MAX(ms, l-ms-1) + 1;
	} else mem0 = l-p;
	mem = 0;

	/* Search loop */
	for (;;) {
		/* If remainder of haystack is shorter than needle, done */
		if (z-h < l) {
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
		for (k=MAX(ms+1,mem); k<l && n[k] == h[k]; k++);
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
void *memmem_uint32(const void *h0, size_t k, const void *n0, size_t l)
{
	const uint32_t *h = (uint32_t*)h0, *n = (uint32_t*)n0;

	/* Return immediately on empty needle */
	if (!l) return (void *)h;

	/* Return immediately when needle is longer than haystack */
	if (k<l) return 0;

	/* Use faster algorithms for short needles */
	h = memmem_one_uint32((uint32_t*)h, *n, h+k);
	if (!h || l == 1) return (void *)h;
	k -= h - (uint32_t*)h0;
	if (k<l) return 0;

	return twoway_memmem_uint32(h, h+k, n, l);
}
