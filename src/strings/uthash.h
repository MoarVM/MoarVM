/*
Copyright (c) 2003-2014, Troy D. Hanson    http://troydhanson.github.com/uthash/
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/* NOTE: While this started out as a stock uthash.h, by now it has
 * undergone numerous changes to more closely integrate it with MoarVM
 * strings, remove things MoarVM doesn't need, and not remember the
 * insertion order (resulting in changes to iteration code - and a
 * memory saving). */

#ifndef UTHASH_H
#define UTHASH_H

#include <string.h>   /* memcmp,strlen */
#include <stddef.h>   /* ptrdiff_t */
#include <stdlib.h>   /* exit() */
#include "strings/siphash/csiphash.h"

/* a number of the hash function use uint32_t which isn't defined on win32 */
#include "platform/inttypes.h"

#define UTHASH_VERSION 1.9.9

/* initial number of buckets */
#define HASH_INITIAL_NUM_BUCKETS 8       /* initial number of buckets        */
#define HASH_INITIAL_NUM_BUCKETS_LOG2 3  /* lg2 of initial number of buckets */
#define HASH_BKT_CAPACITY_THRESH 10      /* expand when bucket count reaches */

MVM_STATIC_INLINE MVMuint64 ptr_hash_64_to_64(MVMuint64 u) {
    /* Thomas Wong's hash from
     * https://web.archive.org/web/20120211151329/http://www.concentric.net/~Ttwang/tech/inthash.htm */
    u = (~u) + (u << 21);
    u =   u  ^ (u >> 24);
    u =  (u  + (u <<  3)) + (u << 8);
    u =   u  ^ (u >> 14);
    u =  (u  + (u <<  2)) + (u << 4);
    u =   u  ^ (u >> 28);
    u =   u  + (u << 31);
    return (MVMuint64)u;
}
MVM_STATIC_INLINE MVMuint32 ptr_hash_32_to_32(MVMuint32 u) {
    /* Bob Jenkins' hash from
     * http://burtleburtle.net/bob/hash/integer.html */
    u = (u + 0x7ed55d16) + (u << 12);
    u = (u ^ 0xc761c23c) ^ (u >> 19);
    u = (u + 0x165667b1) + (u <<  5);
    u = (u + 0xd3a2646c) ^ (u <<  9);
    u = (u + 0xfd7046c5) + (u <<  3);
    u = (u ^ 0xb55a4f09) ^ (u >> 16);
    return (MVMuint32)u;
}
#if 8 <= MVM_PTR_SIZE
#define ptr_hash(u)\
    ptr_hash_64_to_64(u)
#else
#define ptr_hash(u)\
    ptr_hash_32_to_32(u)
#endif

#ifndef ROTL
#define ROTL(x, b) ( ((x) << (b)) | ( (x) >> ((sizeof(x)*8) - (b))) )
#endif

/* We want to put this back at the end: */

#define GET_X_BITS(number, num_bits)\
    ((number) >> ((sizeof(number) * 8) - (num_bits)))

/* This is used since the compiler optimizes it better. */
MVM_STATIC_INLINE MVMHashBktNum GET_X_BITS_BKT_RAND(MVM_UT_bucket_rand bucket_rand, MVMHashUInt num_bits) {
    switch (num_bits) {
        case 1:  return GET_X_BITS(bucket_rand,  1);
        case 2:  return GET_X_BITS(bucket_rand,  2);
        case 3:  return GET_X_BITS(bucket_rand,  3);
        case 4:  return GET_X_BITS(bucket_rand,  4);
        case 5:  return GET_X_BITS(bucket_rand,  5);
        case 6:  return GET_X_BITS(bucket_rand,  6);
        case 7:  return GET_X_BITS(bucket_rand,  7);
        case 8:  return GET_X_BITS(bucket_rand,  8);
        case 9:  return GET_X_BITS(bucket_rand,  9);
        case 10: return GET_X_BITS(bucket_rand, 10);
        case 11: return GET_X_BITS(bucket_rand, 11);
        case 12: return GET_X_BITS(bucket_rand, 12);
        case 13: return GET_X_BITS(bucket_rand, 13);
        case 14: return GET_X_BITS(bucket_rand, 14);
        case 15: return GET_X_BITS(bucket_rand, 15);
        case 16: return GET_X_BITS(bucket_rand, 16);
        case 17: return GET_X_BITS(bucket_rand, 17);
        case 18: return GET_X_BITS(bucket_rand, 18);
        case 19: return GET_X_BITS(bucket_rand, 19);
        case 20: return GET_X_BITS(bucket_rand, 20);
        case 21: return GET_X_BITS(bucket_rand, 21);
        case 22: return GET_X_BITS(bucket_rand, 22);
        case 23: return GET_X_BITS(bucket_rand, 23);
        case 24: return GET_X_BITS(bucket_rand, 24);
        case 25: return GET_X_BITS(bucket_rand, 25);
        case 26: return GET_X_BITS(bucket_rand, 26);
        case 27: return GET_X_BITS(bucket_rand, 27);
        case 28: return GET_X_BITS(bucket_rand, 28);
        case 29: return GET_X_BITS(bucket_rand, 29);
        case 30: return GET_X_BITS(bucket_rand, 30);
        case 31: return GET_X_BITS(bucket_rand, 31);
        default:
            return GET_X_BITS(bucket_rand, num_bits);
    }
}


/* Get a pseudo-random bucket. This works because XORing a random x bit integer
 * with 0..(2**x)-1 will give you 0..(2**x)-1 in a pseudo random order (not *really*
 * random but random enough for our purposes. Example with 0..(2**3)-1 and the rand int is 3
 * 0 ^ 3 = 3; 1 ^ 3 = 2;
 * 2 ^ 3 = 1; 3 ^ 3 = 0;
 * 4 ^ 3 = 7; 5 ^ 3 = 6
 * 6 ^ 3 = 5; 7 ^ 3 = 4 */

#endif /* UTHASH_H */
