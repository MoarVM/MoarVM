#include <stddef.h>
/* <MIT License>
 Copyright (c) 2013  Marek Majkowski <marek@popcount.org>
 Copyright (c) 2018  Samantha McVey <samantham@posteo.net>

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 </MIT License>

 Original location:
        https://github.com/majek/csiphash/

 Original solution inspired by code from:
        Samuel Neves (supercop/crypto_auth/siphash24/little)
        djb (supercop/crypto_auth/siphash24/little2)
        Jean-Philippe Aumasson (https://131002.net/siphash/siphash24.c)

 Extensive modifications for MoarVM by Samantha McVey
*/
/* Define this for our test.c test */
#ifndef MVM_STATIC_INLINE
#define MVM_STATIC_INLINE static
#endif
struct siphash {
    uint64_t v0;
    uint64_t v1;
    uint64_t v2;
    uint64_t v3;
    uint64_t  b;
};
typedef struct siphash siphash;
/* These ifdef's are intended to mostly matter if we are running test.c to test
 * SipHash independent of MoarVM. Eventually we probably want to replace this,
 * but for now, it results in no effect unless MVM_HASH_FORCE_LITTLE_ENDIAN
 * is defined. */
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && \
    __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#  define MVM_TO_LITTLE_ENDIAN_64(x) ((uint64_t)(x))
#  define MVM_TO_LITTLE_ENDIAN_32(x) ((uint32_t)(x))
#elif defined(_WIN32)
/* Windows is always little endian, unless you're on xbox360
   http://msdn.microsoft.com/en-us/library/b0084kay(v=vs.80).aspx */
#  define MVM_TO_LITTLE_ENDIAN_64(x) ((uint64_t)(x))
#  define MVM_TO_LITTLE_ENDIAN_32(x) ((uint32_t)(x))
#elif defined(__APPLE__)
#  include <libkern/OSByteOrder.h>
#  define MVM_TO_LITTLE_ENDIAN_64(x) OSSwapLittleToHostInt64(x)
#  define MVM_TO_LITTLE_ENDIAN_32(x) OSSwapLittleToHostInt32(x)
#else
    /* See: http://sourceforge.net/p/predef/wiki/Endianness/ */
#      if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#        include <sys/endian.h>
#      elif defined(_AIX)
#        include <sys/machine.h>
#      else
#        include <endian.h>
#      endif
#      if defined(__BYTE_ORDER) && defined(__LITTLE_ENDIAN) && \
        __BYTE_ORDER == __LITTLE_ENDIAN
#        define MVM_TO_LITTLE_ENDIAN_64(x) ((uint64_t)(x))
#        define MVM_TO_LITTLE_ENDIAN_32(x) ((uint32_t)(x))
#      else
#        define MVM_TO_LITTLE_ENDIAN_64(x) le64toh(x)
#        define MVM_TO_LITTLE_ENDIAN_32(x) le32toh(x)
#      endif
#endif
#if defined(MVM_HASH_FORCE_LITTLE_ENDIAN)
#    define MVM_MAYBE_TO_LITTLE_ENDIAN_64(x) MVM_TO_LITTLE_ENDIAN_64(x)
#    define MVM_MAYBE_TO_LITTLE_ENDIAN_32(x) MVM_TO_LITTLE_ENDIAN_32(x)
#else
#    define MVM_MAYBE_TO_LITTLE_ENDIAN_64(x) ((uint64_t)(x))
#    define MVM_MAYBE_TO_LITTLE_ENDIAN_32(x) ((uint32_t)(x))
#endif

#if !defined(MVM_CAN_UNALIGNED_INT64)
#   include <string.h>
#endif
#define ROTATE(x, b) (uint64_t)( ((x) << (b)) | ( (x) >> (64 - (b))) )

#define HALF_ROUND(a,b,c,d,s,t) \
    a += b; c += d;             \
    b = ROTATE(b, s) ^ a;       \
    d = ROTATE(d, t) ^ c;       \
    a = ROTATE(a, 32);

#define DOUBLE_ROUND(v0,v1,v2,v3)  \
    HALF_ROUND(v0,v1,v2,v3,13,16); \
    HALF_ROUND(v2,v1,v0,v3,17,21); \
    HALF_ROUND(v0,v1,v2,v3,13,16); \
    HALF_ROUND(v2,v1,v0,v3,17,21);

MVM_STATIC_INLINE void siphashinit (siphash *sh, size_t src_sz, const uint64_t key[2]) {
    const uint64_t k0 = MVM_MAYBE_TO_LITTLE_ENDIAN_64(key[0]);
    const uint64_t k1 = MVM_MAYBE_TO_LITTLE_ENDIAN_64(key[1]);
    sh->b = (uint64_t)src_sz << 56;
    sh->v0 = k0 ^ 0x736f6d6570736575ULL;
    sh->v1 = k1 ^ 0x646f72616e646f6dULL;
    sh->v2 = k0 ^ 0x6c7967656e657261ULL;
    sh->v3 = k1 ^ 0x7465646279746573ULL;
}
MVM_STATIC_INLINE void siphashadd64bits (siphash *sh, const uint64_t in) {
    const uint64_t mi = MVM_MAYBE_TO_LITTLE_ENDIAN_64(in);
    sh->v3 ^= mi;
    DOUBLE_ROUND(sh->v0,sh->v1,sh->v2,sh->v3);
    sh->v0 ^= mi;
}
MVM_STATIC_INLINE uint64_t siphashfinish_last_part (siphash *sh, uint64_t t) {
    sh->b |= MVM_MAYBE_TO_LITTLE_ENDIAN_64(t);
    sh->v3 ^= sh->b;
    DOUBLE_ROUND(sh->v0,sh->v1,sh->v2,sh->v3);
    sh->v0 ^= sh->b;
    sh->v2 ^= 0xff;
    DOUBLE_ROUND(sh->v0,sh->v1,sh->v2,sh->v3);
    DOUBLE_ROUND(sh->v0,sh->v1,sh->v2,sh->v3);
    return (sh->v0 ^ sh->v1) ^ (sh->v2 ^ sh->v3);
}
/* This union helps us avoid doing weird things with pointers that can cause old
 * compilers like GCC 4 to generate bad code. In addition it is nicely more C
 * standards compliant to keep type punning to a minimum. */
union SipHash64_union {
    uint64_t u64;
    uint32_t u32;
    uint8_t  u8[8];
};
MVM_STATIC_INLINE uint64_t siphashfinish_32bits (siphash *sh, const uint32_t src) {
    union SipHash64_union t = { 0 };
    t.u32 = src;
    return siphashfinish_last_part(sh, t.u64);
}
MVM_STATIC_INLINE uint64_t siphashfinish (siphash *sh, const uint8_t *src, size_t src_sz) {
    union SipHash64_union t = { 0 };
    switch (src_sz) {
        /* Falls through */
        case 7: t.u8[6] = src[6];
        /* Falls through */
        case 6: t.u8[5] = src[5];
        /* Falls through */
        case 5: t.u8[4] = src[4];
#if defined(MVM_CAN_UNALIGNED_INT32)
            t.u32 = *((uint32_t*)src);
            break;
#else
        /* Falls through */
#endif
        case 4: t.u8[3] = src[3];
        /* Falls through */
        case 3: t.u8[2] = src[2];
        /* Falls through */
        case 2: t.u8[1] = src[1];
        /* Falls through */
        case 1: t.u8[0] = src[0];
    }
    return siphashfinish_last_part(sh, t.u64);
}
MVM_STATIC_INLINE uint64_t siphash24(const uint8_t *src, size_t src_sz, const uint64_t key[2]) {
    siphash sh;
#if defined(MVM_CAN_UNALIGNED_INT64)
    const uint64_t *in = (uint64_t*)src;
    /* Find largest src_sz evenly divisible by 8 bytes. */
    const ptrdiff_t src_sz_nearest_8bits = (src_sz >> 3) << 3;
    const uint64_t *goal  = (uint64_t*)(src + src_sz_nearest_8bits);
    siphashinit(&sh, src_sz, key);
    src_sz -= src_sz_nearest_8bits;
    while (in < goal) {
        siphashadd64bits(&sh, *in);
        in++;
    }

#else
    const uint8_t *in = src;
    siphashinit(&sh, src_sz, key);
    while (src_sz >= 8) {
        uint64_t in_64;
        memcpy(&in_64, in, sizeof(uint64_t));
        siphashadd64bits(&sh, in_64);
        in += 8; src_sz -= 8;
    }
#endif
    return siphashfinish(&sh, (uint8_t *)in, src_sz);
}
