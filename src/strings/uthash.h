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

#endif /* UTHASH_H */
