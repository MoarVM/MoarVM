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
/* These macros use decltype or the earlier __typeof GNU extension.
   As decltype is only available in newer compilers (VS2010 or gcc 4.3+
   when compiling c++ source) this code uses whatever method is needed
   or, for VS2008 where neither is available, uses casting workarounds. */
#ifdef _MSC_VER         /* MS compiler */
#if _MSC_VER >= 1600 && defined(__cplusplus)  /* VS2010 or newer in C++ mode */
#define DECLTYPE(x) (decltype(x))
#else                   /* VS2008 or older (or VS2010 in C mode) */
#define NO_DECLTYPE
#define DECLTYPE(x)
#endif
#else                   /* GNU, Sun and other compilers */
#define DECLTYPE(x) (__typeof(x))
#endif

#ifdef NO_DECLTYPE
#define DECLTYPE_ASSIGN(dst,src)\
do {\
  char **_da_dst = (char**)(&(dst));\
  *_da_dst = (char*)(src);\
} while(0)
#else
#define DECLTYPE_ASSIGN(dst,src)\
do {\
  (dst) = DECLTYPE(dst)(src);\
} while(0)
#endif

/* a number of the hash function use uint32_t which isn't defined on win32 */
#include "platform/inttypes.h"

#define UTHASH_VERSION 1.9.9

#ifndef uthash_fatal
#error "uthash_fatal not defined"
#define uthash_fatal(msg) exit(-1)        /* fatal error (out of memory,etc) */
#endif
#ifndef uthash_malloc
#define uthash_malloc(tc, sz) MVM_fixed_size_alloc(tc, tc->instance->fsa, sz)      /* malloc fcn                      */
#endif
#ifndef uthash_malloc_zeroed
#define uthash_malloc_zeroed(tc, sz) MVM_fixed_size_alloc_zeroed(tc, tc->instance->fsa, sz)      /* malloc fcn                      */
#endif
#ifndef uthash_free
#define uthash_free(tc, ptr, sz) MVM_fixed_size_free(tc, tc->instance->fsa, sz, ptr)     /* free fcn                        */
#endif

#ifndef uthash_noexpand_fyi
#define uthash_noexpand_fyi(tbl)          /* can be defined to log noexpand  */
#endif
#ifndef uthash_expand_fyi
#define uthash_expand_fyi(tbl)            /* can be defined to log expands   */
#endif

/* initial number of buckets */
#define HASH_INITIAL_NUM_BUCKETS 8       /* initial number of buckets        */
#define HASH_INITIAL_NUM_BUCKETS_LOG2 3  /* lg2 of initial number of buckets */
#define HASH_BKT_CAPACITY_THRESH 10      /* expand when bucket count reaches */

#include "strings/uthash_types.h"
void MVM_fixed_size_free(MVMThreadContext *tc, MVMFixedSizeAlloc *fsa, size_t bytes, void *free);
void * MVM_fixed_size_alloc(MVMThreadContext *tc, MVMFixedSizeAlloc *fsa, size_t bytes);
void * MVM_fixed_size_alloc_zeroed(MVMThreadContext *tc, MVMFixedSizeAlloc *fsa, size_t bytes);
/* calculate the element whose hash handle address is hhe */
#define ELMT_FROM_HH(tbl,hhp) ((void*)(((char*)(hhp)) - ((tbl)->hho)))

#define HASH_FIND_VM_STR_AND_DELETE(tc,hh,head,key,out,prev) do {\
    HASH_FIND_VM_STR_prev(tc,hh,head,key,out,prev);\
    if (out)\
        HASH_DELETE(hh,head,out,prev);\
} while (0)

#define HASH_FIND_AND_DELETE(hh,head,keyptr,keylen,out,prev) do {\
    HASH_FIND_prev(hh,head,keyptr,keylen,out,prev);\
    if (out)\
        HASH_DELETE(hh,head,out,prev);\
} while (0)
#define HASH_FIND(hh,head,keyptr,keylen,out) do {\
    MVMHashv _hf_hashv;\
    MVMHashBktNum _hf_bkt;\
    out=NULL;\
    if (head) {\
        HASH_FCN(keyptr,keylen, (head)->hh.tbl->num_buckets, _hf_hashv, _hf_bkt,\
                (head)->hh.tbl->log2_num_buckets);\
        HASH_FIND_IN_BKT((head)->hh.tbl, hh, (head)->hh.tbl->buckets[ _hf_bkt ],\
                          keyptr,keylen,out,_hf_hashv);\
    }\
} while (0)
#define HASH_FIND_prev(hh,head,keyptr,keylen,out,prev) do {\
    MVMHashv _hf_hashv;\
    MVMHashBktNum _hf_bkt;\
    out=NULL;\
    prev=NULL;\
    if (head) {\
        HASH_FCN(keyptr,keylen, (head)->hh.tbl->num_buckets, _hf_hashv, _hf_bkt,\
                (head)->hh.tbl->log2_num_buckets);\
        HASH_FIND_IN_BKT_prev((head)->hh.tbl, hh, (head)->hh.tbl->buckets[ _hf_bkt ],\
                      keyptr,keylen,out,_hf_hashv, prev);\
  }\
} while (0)
#define DETERMINE_BUCKET_AND(hashv, num_bkts)\
    ((hashv) & ((num_bkts) - 1))
/* Fibonacci bucket determination.
 * Since we grow bucket sizes in multiples of two, we just need a right
 * bitmask to get it on the correct scale. This has an advantage over using &ing
 * or % to get the bucket number because it uses the full bit width of the hash.
 * If the size of the hashv is changed we will need to change max_hashv_div_phi,
 * to be max_hashv / phi rounded to the nearest *odd* number.
 * max_hashv / phi = 11400714819323198485 */
#define max_hashv_div_phi UINT64_C(11400714819323198485)
#define DETERMINE_BUCKET_FIB(hashv, offset)\
    (((hashv) * max_hashv_div_phi) >> ((sizeof(MVMHashv)*8) - offset))

#define WHICH_BUCKET(hashv, num_bkts, offset)\
    (DETERMINE_BUCKET_FIB((hashv), (offset)))

#define HASH_FIND_VM_STR(tc,hh,head,key,out)\
do {\
    MVMHashv _hf_hashv;\
    MVMHashBktNum _hf_bkt;\
    out=NULL;\
    if (head) {\
        MVMHashv cached_hash = (key)->body.cached_hash_code;\
        if (cached_hash) {\
            _hf_hashv = cached_hash;\
            _hf_bkt = WHICH_BUCKET((_hf_hashv), (head)->hh.tbl->num_buckets, (head)->hh.tbl->log2_num_buckets);\
        }\
        else {\
            HASH_FCN_VM_STR(tc, key, (head)->hh.tbl->num_buckets, _hf_hashv, _hf_bkt, (head)->hh.tbl->log2_num_buckets);\
        }\
        HASH_FIND_IN_BKT_VM_STR(tc, (head)->hh.tbl, hh,\
            (head)->hh.tbl->buckets[ _hf_bkt ], key, out, _hf_hashv);\
    }\
} while (0)

#define HASH_FIND_VM_STR_prev(tc,hh,head,key,out, prev)\
do {\
    MVMHashv _hf_hashv;\
    MVMHashBktNum _hf_bkt;\
    out=NULL;\
    prev=NULL;\
    if (head) {\
        MVMHashv cached_hash = (key)->body.cached_hash_code;\
        if (cached_hash) {\
            _hf_hashv = cached_hash;\
            _hf_bkt = WHICH_BUCKET((_hf_hashv), (head)->hh.tbl->num_buckets, (head)->hh.tbl->log2_num_buckets);\
        }\
        else {\
            HASH_FCN_VM_STR(tc, key, (head)->hh.tbl->num_buckets, _hf_hashv, _hf_bkt, (head)->hh.tbl->log2_num_buckets);\
        }\
        HASH_FIND_IN_BKT_VM_STR_prev(tc, (head)->hh.tbl, hh,\
            (head)->hh.tbl->buckets[ _hf_bkt ], key, out, _hf_hashv, prev);\
    }\
} while (0)

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
MVM_PUBLIC MVM_NO_RETURN void MVM_exception_throw_adhoc(MVMThreadContext *tc, const char *messageFormat, ...) MVM_NO_RETURN_ATTRIBUTE MVM_FORMAT(printf, 2, 3);
MVM_STATIC_INLINE void HASH_MAKE_TABLE(MVMThreadContext *tc, void *head, UT_hash_handle *head_hh) {
    head_hh->tbl = (UT_hash_table*)uthash_malloc_zeroed(tc, sizeof(UT_hash_table));
    head_hh->tbl->num_buckets = HASH_INITIAL_NUM_BUCKETS;
    head_hh->tbl->log2_num_buckets = HASH_INITIAL_NUM_BUCKETS_LOG2;
    head_hh->tbl->hho = (char*)(head_hh) - (char*)(head);
    head_hh->tbl->buckets = (UT_hash_bucket*)uthash_malloc_zeroed(tc,
         HASH_INITIAL_NUM_BUCKETS * sizeof(struct UT_hash_bucket));
    /* Hash the pointer. We use this hash to randomize insertion order and randomize
     * iteration order. */
#if MVM_HASH_RANDOMIZE
    head_hh->tbl->bucket_rand = ptr_hash((uintptr_t)head_hh);
#endif
}

#define HASH_ADD_KEYPTR(hh,head,keyptr,keylen_in,add) do {\
    MVMHashBktNum _ha_bkt;\
    (add)->hh.key = (char*)(keyptr);\
    (add)->hh.keylen = (MVMHashKeyLen)(keylen_in);\
    if (!(head)) {\
        head = (add);\
        HASH_MAKE_TABLE(tc, head, &((head)->hh));\
    }\
    (head)->hh.tbl->num_items++;\
    (add)->hh.tbl = (head)->hh.tbl;\
    HASH_FCN(keyptr,keylen_in, (head)->hh.tbl->num_buckets,\
         (add)->hh.hashv, _ha_bkt, (head)->hh.tbl->log2_num_buckets);\
    HASH_ADD_TO_BKT(tc, &((head)->hh.tbl->buckets[_ha_bkt]),&(add)->hh, (head)->hh.tbl);\
    HASH_FSCK(hh,head);\
} while(0)

#define HASH_ADD_KEYPTR_VM_STR(tc,hh,head,key_in,add)\
do {\
    MVMHashBktNum _ha_bkt;\
    MVMHashv cached_hash = (key_in)->body.cached_hash_code;\
    (add)->hh.key = (key_in);\
    if (!(head)) {\
        head = (add);\
        HASH_MAKE_TABLE(tc, head, &((head)->hh));\
    }\
    (head)->hh.tbl->num_items++;\
    (add)->hh.tbl = (head)->hh.tbl;\
    if (cached_hash) {\
        (add)->hh.hashv = cached_hash;\
        _ha_bkt = WHICH_BUCKET((cached_hash),((head)->hh.tbl->num_buckets),\
                              (head)->hh.tbl->log2_num_buckets);\
 }\
 else {\
     HASH_FCN_VM_STR(tc, key_in, (head)->hh.tbl->num_buckets,\
        (add)->hh.hashv, _ha_bkt, (head)->hh.tbl->log2_num_buckets);\
 }\
 HASH_ADD_TO_BKT(tc, &((head)->hh.tbl->buckets[_ha_bkt]),&(add)->hh, (head)->hh.tbl);\
 HASH_FSCK(hh,head);\
} while(0)

#define HASH_TO_BKT( hashv, num_bkts, bkt, offset )\
do {\
  bkt = WHICH_BUCKET((hashv), (num_bkts), (offset));\
} while(0)

/* delete "delptr" from the hash table.
 * The use of _hd_hh_del below deserves special explanation.
 * These used to be expressed using (delptr) but that led to a bug
 * if someone used the same symbol for the head and deletee, like
 *  HASH_DELETE(hh,users,users);
 * We want that to work, but by changing the head (users) below
 * we were forfeiting our ability to further refer to the deletee (users)
 * in the patch-up process. Solution: use scratch space to
 * copy the deletee pointer, then the latter references are via that
 * scratch pointer rather than through the repointed (users) symbol.
 */

/* Use HASH_DELETE normally. If you get compilation errors due to the macro defining
 * the same goto label multiple times, use HASH_DELETE_MACRO which lets you
 * specify a prefix for the goto label which will prevent the conflict. */
#define HASH_DELETE(hh,head,delptr,prevptr)\
    HASH_DELETE_MACRO(hh,head,delptr,prevptr, MVM_HASH)
#define HASH_DELETE_MACRO(hh,head,delptr,prevptr,label) do {\
    MVMHashBktNum _hd_bkt;\
    struct UT_hash_handle *_hd_hh_del, *_hd_hh_prevptr;\
    if ( (head)->hh.tbl->num_items == 1 )  {\
        uthash_free(tc, (head)->hh.tbl->buckets,\
                   (head)->hh.tbl->num_buckets * sizeof(struct UT_hash_bucket) );\
        uthash_free(tc, (head)->hh.tbl, sizeof(UT_hash_table));\
        (head) = NULL;\
    }\
    else {\
        _hd_hh_del = &((delptr)->hh);\
        _hd_hh_prevptr = prevptr ? &((prevptr)->hh) : NULL;\
        if ((delptr) == (head)) {\
            MVMHashBktNum cur = 0;\
            while (cur < (head)->hh.tbl->num_buckets) {\
                UT_hash_handle *cand = (head)->hh.tbl->buckets[cur].hh_head;\
                while (cand) {\
                    if (cand && cand != &((delptr)->hh)) {\
                        DECLTYPE_ASSIGN((head), ELMT_FROM_HH((head)->hh.tbl,cand));\
                        goto label ## REPLACED_HEAD;\
                    }\
                    cand = cand->hh_next;\
                }\
                cur++;\
            }\
            uthash_fatal("Failed to replace deleted head");\
            label ## REPLACED_HEAD:\
            ;\
        }\
        _hd_bkt = WHICH_BUCKET( _hd_hh_del->hashv, (head)->hh.tbl->num_buckets,\
                              (head)->hh.tbl->log2_num_buckets);\
        HASH_DEL_IN_BKT(&((head)->hh.tbl->buckets[_hd_bkt]), _hd_hh_del, _hd_hh_prevptr);\
        (head)->hh.tbl->num_items--;\
    }\
    HASH_FSCK(hh,head);\
} while (0)

/* This delete's a pointer by address from the hash. */
#define HASH_DELETE_PTR(tc, hh, hash, delptr, hashentry_type) do {\
    struct UT_hash_table *ht;\
    if (hash && (ht = hash->hh.tbl)) {\
        MVMHashBktNum bucket_tmp = WHICH_BUCKET((delptr)->hh.hashv,\
            (delptr)->hh.tbl->num_buckets,\
            (delptr)->hh.tbl->log2_num_buckets);\
        struct UT_hash_handle *current_hh = ht->buckets[bucket_tmp].hh_head;\
        hashentry_type *prev = NULL, *current = NULL;\
        while (current_hh) {\
            current = ELMT_FROM_HH(ht, current_hh);\
            if ((delptr) == current) {\
                HASH_DELETE(hh, (hash), (current), (prev));\
                current_hh = NULL;\
            }\
            else {\
                current_hh = current_hh->hh_next;\
                prev = current;\
            }\
        }\
    }\
    HASH_FSCK(hh, hash);\
} while (0)

/* HASH_FSCK checks hash integrity on every add/delete when HASH_DEBUG is defined.
 * This is for uthash developer only; it compiles away if HASH_DEBUG isn't defined.
 */

#ifdef HASH_DEBUG
#define HASH_OOPS(...) do { fprintf(stderr,__VA_ARGS__); exit(-1); } while (0)
#define HASH_FSCK(hh,head) do {\
    MVMuint64 _bkt_i;\
    MVMuint64 _count, _bkt_count;\
    char *_prev;\
    struct UT_hash_handle *_thh;\
    if (head) {\
        _count = 0;\
        for( _bkt_i = 0; _bkt_i < (head)->hh.tbl->num_buckets; _bkt_i++) {\
            _bkt_count = 0;\
            _thh = (head)->hh.tbl->buckets[_bkt_i].hh_head;\
            _prev = NULL;\
            while (_thh) {\
                MVMuint64 expected_bkt = WHICH_BUCKET(_thh->hashv, (head)->hh.tbl->num_buckets, (head)->hh.tbl->log2_num_buckets);\
                _bkt_count++;\
                if (expected_bkt != _bkt_i)\
                    HASH_OOPS("Hash item is in the wrong bucket. Should be in %"PRIu64" but is actually in %"PRIu64"\n", expected_bkt, _bkt_i);\
                _prev = (char*)(_thh);\
                _thh = _thh->hh_next;\
            }\
            _count += _bkt_count;\
            if ((head)->hh.tbl->buckets[_bkt_i].count !=  _bkt_count) {\
                HASH_OOPS("invalid bucket count %d, actual %d\n",\
                    (head)->hh.tbl->buckets[_bkt_i].count, _bkt_count);\
            }\
        }\
        if (_count != (head)->hh.tbl->num_items) {\
            HASH_OOPS("invalid hh item count %d, actual %d\n",\
                (head)->hh.tbl->num_items, _count );\
        }\
    }\
} while (0)
#else
#define HASH_FSCK(hh,head)
#endif

/* Use Siphash as the hash function. */
#define HASH_FCN HASH_SIP
#define HASH_FCN_VM_STR HASH_SIP_VM_STR

#define HASH_JEN_MIX(a,b,c) do {\
    a -= b; a -= c; a ^= ( c >> 13 );\
    b -= c; b -= a; b ^= ( a << 8 );\
    c -= a; c -= b; c ^= ( b >> 13 );\
    a -= b; a -= c; a ^= ( c >> 12 );\
    b -= c; b -= a; b ^= ( a << 16 );\
    c -= a; c -= b; c ^= ( b >> 5 );\
    a -= b; a -= c; a ^= ( c >> 3 );\
    b -= c; b -= a; b ^= ( a << 10 );\
    c -= a; c -= b; c ^= ( b >> 15 );\
} while (0)
#define HASH_SIP(key, keylen, num_bkts,hashv, bkt, offset)\
do {\
    hashv = siphash24((MVMuint8*)key, keylen, tc->instance->hashSecrets);\
    bkt = WHICH_BUCKET(hashv, num_bkts, offset);\
} while (0)
#define HASH_JEN(key,keylen,num_bkts,hashv,bkt,offset) do {\
    MVMuint32 _hj_i,_hj_j,_hj_k;\
    MVMuint8 *_hj_key=(MVMuint8 *)(key);\
    hashv = tc->instance->hashSecrets[1];\
    _hj_i = _hj_j = 0x9e3779b9;\
    _hj_k = (MVMuint32)(keylen);\
    while (_hj_k >= 12) {\
        _hj_i += (_hj_key[0] + ( (MVMuint32)_hj_key[1] << 8 )\
              + ( (MVMuint32)_hj_key[2] << 16 )\
              + ( (MVMuint32)_hj_key[3] << 24 ) );\
        _hj_j += (_hj_key[4] + ( (MVMuint32)_hj_key[5] << 8 )\
              + ( (MVMuint32)_hj_key[6] << 16 )\
              + ( (MVMuint32)_hj_key[7] << 24 ) );\
        hashv += (_hj_key[8] + ( (MVMuint32)_hj_key[9] << 8 )\
              + ( (MVMuint32)_hj_key[10] << 16 )\
              + ( (MVMuint32)_hj_key[11] << 24 ) );\\
        HASH_JEN_MIX(_hj_i, _hj_j, hashv);\
        _hj_key += 12;\
        _hj_k -= 12;\
    }\
    hashv += keylen;\
    switch ( _hj_k ) {\
        case 11: hashv += ( (MVMuint32)_hj_key[10] << 24 );\
        case 10: hashv += ( (MVMuint32)_hj_key[9]  << 16 );\
        case 9:  hashv += ( (MVMuint32)_hj_key[8]  <<  8 );\
        case 8:  _hj_j += ( (MVMuint32)_hj_key[7]  << 24 );\
        case 7:  _hj_j += ( (MVMuint32)_hj_key[6]  << 16 );\
        case 6:  _hj_j += ( (MVMuint32)_hj_key[5]  <<  8 );\
        case 5:  _hj_j += _hj_key[4];\
        case 4:  _hj_i += ( (MVMuint32)_hj_key[3]  << 24 );\
        case 3:  _hj_i += ( (MVMuint32)_hj_key[2]  << 16 );\
        case 2:  _hj_i += ( (MVMuint32)_hj_key[1]  <<  8 );\
        case 1:  _hj_i += _hj_key[0];\
    }\
    HASH_JEN_MIX(_hj_i, _hj_j, hashv);\
    if (hashv == 0) {\
        hashv += keylen;\
    }\
    bkt = WHICH_BUCKET(hashv, num_bkts, offset);\
} while(0)

#define HASH_SIP_VM_STR(tc,key,num_bkts,hashv,bkt,offset) do {\
    MVM_string_compute_hash_code((tc), (key));\
    (hashv) = (key)->body.cached_hash_code;\
    (bkt) = WHICH_BUCKET((hashv), (num_bkts), (offset));\
} while(0)

/* key comparison function; return 0 if keys equal */
#define HASH_KEYCMP(a,b,len) memcmp(a,b,len)

/* iterate over items in a known bucket to find desired item */
#define HASH_FIND_IN_BKT(tbl,hh,head,keyptr,keylen_in,out, hashval) do {\
    if (head.hh_head) DECLTYPE_ASSIGN(out,ELMT_FROM_HH(tbl,head.hh_head));\
    else out=NULL;\
    while (out) {\
        if ((out)->hh.hashv == hashval && (out)->hh.keylen == keylen_in) {\
            if ((HASH_KEYCMP((out)->hh.key,keyptr,keylen_in)) == 0)\
                break;\
        }\
        if ((out)->hh.hh_next)\
            DECLTYPE_ASSIGN(out,ELMT_FROM_HH(tbl,(out)->hh.hh_next));\
        else\
            out = NULL;\
 }\
} while(0)


/* iterate over items in a known bucket to find desired item */
#define HASH_FIND_IN_BKT_prev(tbl,hh,head,keyptr,keylen_in,out,hashval,prev)\
do {\
    if (head.hh_head) DECLTYPE_ASSIGN(out,ELMT_FROM_HH(tbl,head.hh_head));\
    else prev = out = NULL;\
    while (out) {\
        if ((out)->hh.hashv == hashval && (out)->hh.keylen == keylen_in) {\
            if ((HASH_KEYCMP((out)->hh.key,keyptr,keylen_in)) == 0)\
                break;\
        }\
        if ((out)->hh.hh_next) {\
            prev = out;\
            DECLTYPE_ASSIGN(out,ELMT_FROM_HH(tbl,(out)->hh.hh_next));\
        }\
        else {\
            prev = out = NULL;\
        }\
    }\
} while(0)

/* iterate over items in a known bucket to find desired item */
#define HASH_FIND_IN_BKT_VM_STR(tc,tbl,hh,head,key_in,out,hashval)\
do {\
    if (head.hh_head) DECLTYPE_ASSIGN(out,ELMT_FROM_HH(tbl,head.hh_head));\
    else out=NULL;\
    while (out) {\
        if (hashval == (out)->hh.hashv) {\
            MVMString *key_out = (MVMString *)((out)->hh.key);\
            if ((key_in) == key_out || MVM_string_substrings_equal_nocheck(tc, (key_in), 0,\
                    (key_in)->body.num_graphs, key_out, 0))\
                break;\
        }\
        if ((out)->hh.hh_next)\
            DECLTYPE_ASSIGN(out,ELMT_FROM_HH(tbl,(out)->hh.hh_next));\
        else\
            out = NULL;\
    }\
} while(0)

/* iterate over items in a known bucket to find desired item */
#define HASH_FIND_IN_BKT_VM_STR_prev(tc,tbl,hh,head,key_in,out,hashval, prev)\
do {\
 if (head.hh_head) DECLTYPE_ASSIGN(out,ELMT_FROM_HH(tbl,head.hh_head));\
 else {\
     out=NULL;\
     prev=NULL;\
 }\
 while (out) {\
    if (hashval == (out)->hh.hashv && MVM_string_equal(tc, (key_in), (MVMString *)((out)->hh.key)))\
        break;\
    if ((out)->hh.hh_next)  {\
        prev = out;\
        DECLTYPE_ASSIGN(out,ELMT_FROM_HH(tbl,(out)->hh.hh_next));\
    }\
    else {\
        out = NULL;\
        prev = NULL;\
    }\
 }\
} while(0)

/* Bucket expansion has the effect of doubling the number of buckets
 * and redistributing the items into the new buckets. Ideally the
 * items will distribute more or less evenly into the new buckets
 * (the extent to which this is true is a measure of the quality of
 * the hash function as it applies to the key domain).
 *
 * With the items distributed into more buckets, the chain length
 * (item count) in each bucket is reduced. Thus by expanding buckets
 * the hash keeps a bound on the chain length. This bounded chain
 * length is the essence of how a hash provides constant time lookup.
 *
 * The calculation of tbl->ideal_chain_maxlen below deserves some
 * explanation. First, keep in mind that we're calculating the ideal
 * maximum chain length based on the *new* (doubled) bucket count.
 * In fractions this is just n/b (n=number of items,b=new num buckets).
 * Since the ideal chain length is an integer, we want to calculate
 * ceil(n/b). We don't depend on floating point arithmetic in this
 * hash, so to calculate ceil(n/b) with integers we could write
 *
 *      ceil(n/b) = (n/b) + ((n%b)?1:0)
 *
 * and in fact a previous version of this hash did just that.
 * But now we have improved things a bit by recognizing that b is
 * always a power of two. We keep its base 2 log handy (call it lb),
 * so now we can write this with a bit shift and logical AND:
 *
 *      ceil(n/b) = (n>>lb) + ( (n & (b-1)) ? 1:0)
 *
 */
MVM_STATIC_INLINE void HASH_EXPAND_BUCKETS(MVMThreadContext *tc, UT_hash_table *tbl) {
    MVMHashBktNum he_bkt;
    MVMHashBktNum he_bkt_i;
    struct UT_hash_handle *he_thh, *_he_hh_nxt;
    UT_hash_bucket *he_new_buckets, *_he_newbkt;
    MVMHashBktNum new_num_bkts = tbl->num_buckets * 2;
    MVMHashUInt new_log2_num_buckets = tbl->log2_num_buckets + 1;
    he_new_buckets =
        uthash_malloc_zeroed(tc, new_num_bkts * sizeof(struct UT_hash_bucket));
    tbl->ideal_chain_maxlen =
        (tbl->num_items >> new_log2_num_buckets) +
        ((tbl->num_items & (new_num_bkts-1)) ? 1 : 0);
    tbl->nonideal_items = 0;
    /* Iterate the buckets */
    for(he_bkt_i = 0; he_bkt_i < tbl->num_buckets; he_bkt_i++) {
        he_thh = tbl->buckets[ he_bkt_i ].hh_head;
        /* Iterate items in the bucket */
        while (he_thh) {
            _he_hh_nxt = he_thh->hh_next;
            he_bkt = WHICH_BUCKET( he_thh->hashv, new_num_bkts, new_log2_num_buckets);
            _he_newbkt = &(he_new_buckets[ he_bkt ]);
            if (++(_he_newbkt->count) > tbl->ideal_chain_maxlen) {
                tbl->nonideal_items++;
                _he_newbkt->expand_mult = _he_newbkt->count /
                                          tbl->ideal_chain_maxlen;
           }
           he_thh->hh_next = _he_newbkt->hh_head;
           _he_newbkt->hh_head = he_thh;
           he_thh = _he_hh_nxt;
        }
    }
    uthash_free(tc, tbl->buckets, tbl->num_buckets*sizeof(struct UT_hash_bucket) );
    tbl->num_buckets = new_num_bkts;
    tbl->log2_num_buckets = new_log2_num_buckets;
    tbl->buckets = he_new_buckets;
    tbl->ineff_expands = (tbl->nonideal_items > (tbl->num_items >> 1))
        ? (tbl->ineff_expands+1)
        : 0;
    if (tbl->ineff_expands > 1) {
        tbl->noexpand=1;
        uthash_noexpand_fyi(tbl);
    }
    uthash_expand_fyi(tbl);
}

/* add an item to a bucket  */
MVM_STATIC_INLINE void HASH_ADD_TO_BKT(MVMThreadContext *tc, UT_hash_bucket *bucket, UT_hash_handle *addhh, UT_hash_table *table) {
#if MVM_HASH_RANDOMIZE
    table->bucket_rand = bucket->hh_head ? ROTL(table->bucket_rand, 1) : table->bucket_rand + 1;/* Rotate so our bucket_rand changes somewhat. */
    if (bucket->hh_head && table->bucket_rand & 1) {
        UT_hash_handle *head = bucket->hh_head;
        addhh->hh_next = head->hh_next;
        head->hh_next = addhh;
    }
    else {
#endif
        addhh->hh_next = bucket->hh_head;
        bucket->hh_head = addhh;
#if MVM_HASH_RANDOMIZE
    }
#endif
    if (++(bucket->count) >= ((bucket->expand_mult+1) * HASH_BKT_CAPACITY_THRESH)
     && addhh->tbl->noexpand != 1) {
         HASH_EXPAND_BUCKETS(tc, addhh->tbl);
    }
}


/* remove an item from a given bucket */
MVM_STATIC_INLINE void HASH_DEL_IN_BKT(UT_hash_bucket *head, UT_hash_handle *hh_del, UT_hash_handle *hh_prev) {
    head->count--;
    if (head->hh_head == hh_del) {
        head->hh_head = hh_del->hh_next;
    }
    if (hh_prev) {
        hh_prev->hh_next = hh_del->hh_next;
    }
}


#define HASH_CLEAR(tc,hh,head) do {\
    if (head) {\
        uthash_free(tc, (head)->hh.tbl->buckets,\
                   (head)->hh.tbl->num_buckets*sizeof(struct UT_hash_bucket));\
        uthash_free(tc, (head)->hh.tbl, sizeof(UT_hash_table));\
        (head)=NULL;\
    }\
} while(0)
#define GET_X_BITS(number, num_bits)\
    ((number) >> ((sizeof(number) * 8) - (num_bits)))
/* obtain a count of items in the hash */
#define HASH_CNT(hh,head) ((head)?((head)->hh.tbl->num_items):0)
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
#if MVM_HASH_RANDOMIZE
#define GET_PRAND_BKT(raw_bkt_num, hashtable)\
    (GET_X_BITS_BKT_RAND(hashtable->bucket_rand, hashtable->log2_num_buckets) ^ (raw_bkt_num))
#else
#define GET_PRAND_BKT(raw_bkt_num, hashtable) (raw_bkt_num)
#endif
MVM_STATIC_INLINE void * HASH_ITER_FIRST_ITEM(
        struct UT_hash_table *ht, MVMHashBktNum *bucket_tmp) {
    if (!ht)
        return NULL;
#if MVM_HASH_THROW_ON_ITER_AFTER_ADD_KEY
    ht->bucket_rand_last = ht->bucket_rand;
#endif
    while (*bucket_tmp < ht->num_buckets) {
        struct UT_hash_handle *hh_head = ht->buckets[GET_PRAND_BKT(*bucket_tmp, ht)].hh_head;
        if (hh_head)
            return ELMT_FROM_HH(ht, hh_head);
        (*bucket_tmp)++;
    }
    return NULL;
}
/* This is an optimized version of HASH_ITER which doesn't do any iteration
 * order randomization. This version is faster and should be used to iterate
 * through which the iteration order is not exposed to the user. */
#define HASH_ITER_FAST(tc, hh, hash, current, code) do {\
    MVMHashBktNum bucket_tmp = 0;\
    struct UT_hash_table *ht;\
    if (hash && (ht = hash->hh.tbl)) {\
        while (bucket_tmp < ht->num_buckets) {\
            struct UT_hash_handle *current_hh = ht->buckets[bucket_tmp].hh_head;\
            while (current_hh) {\
                current = ELMT_FROM_HH(ht, current_hh);\
                current_hh = current_hh->hh_next;\
                code\
            }\
            (bucket_tmp)++;\
        }\
    }\
} while (0)

#define HASH_ITER(tc, hh, hash, current, code) do {\
    MVMHashBktNum bucket_tmp = 0;\
    struct UT_hash_table *ht;\
    if (hash && (ht = hash->hh.tbl)) {\
        while (bucket_tmp < ht->num_buckets) {\
            struct UT_hash_handle *current_hh =\
                ht->buckets[GET_PRAND_BKT(bucket_tmp, ht)].hh_head;\
            while (current_hh) {\
                current = ELMT_FROM_HH(ht, current_hh);\
                current_hh = current_hh->hh_next;\
                code\
            }\
            (bucket_tmp)++;\
        }\
    }\
} while (0)


MVM_STATIC_INLINE void * HASH_ITER_NEXT_ITEM(MVMThreadContext *tc,
        struct UT_hash_handle *cur_handle, MVMHashBktNum *bucket_tmp) {
    struct UT_hash_table *ht = cur_handle->tbl;
#if MVM_HASH_THROW_ON_ITER_AFTER_ADD_KEY
    /* Warn if the user has been *caught* inserting keys during an iteration. */
    if (ht->bucket_rand_last != ht->bucket_rand)
        MVM_exception_throw_adhoc(tc, "Warning: trying to insert into the hash while iterating has undefined functionality\n");
#endif
    if (cur_handle->hh_next)
        return ELMT_FROM_HH(ht, cur_handle->hh_next);
    (*bucket_tmp)++;
    while (*bucket_tmp < ht->num_buckets) {
        struct UT_hash_handle *hh_head = ht->buckets[GET_PRAND_BKT(*bucket_tmp, ht)].hh_head;
        if (hh_head)
            return ELMT_FROM_HH(ht, hh_head);
        (*bucket_tmp)++;
    }
    return NULL;
}

#endif /* UTHASH_H */
