/*
Copyright (c) 2003-2014, Troy D. Hanson     http://troydhanson.github.com/uthash/
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
#define DECLTYPE_ASSIGN(dst,src)                                                 \
do {                                                                             \
  char **_da_dst = (char**)(&(dst));                                             \
  *_da_dst = (char*)(src);                                                       \
} while(0)
#else
#define DECLTYPE_ASSIGN(dst,src)                                                 \
do {                                                                             \
  (dst) = DECLTYPE(dst)(src);                                                    \
} while(0)
#endif

/* a number of the hash function use uint32_t which isn't defined on win32 */
#ifdef _MSC_VER
typedef unsigned int uint32_t;
typedef unsigned char uint8_t;
#else
#include <inttypes.h>   /* uint32_t */
#endif

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
#define HASH_FIND(hh,head,keyptr,keylen,out)                                     \
do {                                                                             \
  MVMhashv _hf_hashv;                                                            \
  unsigned _hf_bkt;                                                              \
  out=NULL;                                                                      \
  if (head) {                                                                    \
     HASH_FCN(keyptr,keylen, (head)->hh.tbl->num_buckets, _hf_hashv, _hf_bkt, (head)->hh.tbl->log2_num_buckets); \
     HASH_FIND_IN_BKT((head)->hh.tbl, hh, (head)->hh.tbl->buckets[ _hf_bkt ],  \
                      keyptr,keylen,out,_hf_hashv);                             \
  }                                                                              \
} while (0)
#define DETERMINE_BUCKET_AND(hashv, num_bkts) \
    ((hashv) & ((num_bkts) - 1))
/* Fibonacci bucket determination.
 * Since we grow bucket sizes in multiples of two, we just need a right
 * bitmask to get it on the correct scale. This has an advantage over using &ing
 * or % to get the bucket number because it uses the full bit width of the hash.
 * If the size of the hashv is changed we will need to change max_hashv_div_phi,
 * to be max_hashv / phi rounded to the nearest *odd* number.
 * max_hashv / phi = 2654435769 */
const static uint32_t max_hashv_div_phi = 2654435769;
#define DETERMINE_BUCKET_FIB(hashv, offset) \
    (((hashv) * max_hashv_div_phi) >> (32 - offset))

#define WHICH_BUCKET(hashv, num_bkts, offset)\
    (DETERMINE_BUCKET_FIB((hashv), (offset)))

#define HASH_FIND_VM_STR(tc,hh,head,key,out)                                        \
do {                                                                                \
  MVMhashv _hf_hashv;                                                               \
  unsigned _hf_bkt;                                                                 \
  out=NULL;                                                                         \
  if (head) {                                                                       \
     MVMhashv cached_hash = (key)->body.cached_hash_code;                           \
     if (cached_hash) {                                                             \
         _hf_hashv = cached_hash;                                                   \
         _hf_bkt = WHICH_BUCKET((_hf_hashv), (head)->hh.tbl->num_buckets, (head)->hh.tbl->log2_num_buckets); \
     }                                                                              \
     else {                                                                         \
         HASH_FCN_VM_STR(tc, key, (head)->hh.tbl->num_buckets, _hf_hashv, _hf_bkt, (head)->hh.tbl->log2_num_buckets); \
     }                                                                              \
     HASH_FIND_IN_BKT_VM_STR(tc, (head)->hh.tbl, hh,                                \
         (head)->hh.tbl->buckets[ _hf_bkt ], key, out, _hf_hashv);                  \
  }                                                                                 \
} while (0)
MVM_PUBLIC MVM_NO_RETURN void MVM_exception_throw_adhoc(MVMThreadContext *tc, const char *messageFormat, ...) MVM_NO_RETURN_ATTRIBUTE MVM_FORMAT(printf, 2, 3);
MVM_STATIC_INLINE void HASH_MAKE_TABLE(MVMThreadContext *tc, void *head, UT_hash_handle *head_hh) {
  head_hh->tbl = (UT_hash_table*)uthash_malloc_zeroed(tc,
                  sizeof(UT_hash_table));
  head_hh->tbl->num_buckets = HASH_INITIAL_NUM_BUCKETS;
  head_hh->tbl->log2_num_buckets = HASH_INITIAL_NUM_BUCKETS_LOG2;
  head_hh->tbl->hho = (char*)(head_hh) - (char*)(head);
  head_hh->tbl->buckets = (UT_hash_bucket*)uthash_malloc_zeroed(tc,
          HASH_INITIAL_NUM_BUCKETS*sizeof(struct UT_hash_bucket));
}

#define HASH_ADD_KEYPTR(hh,head,keyptr,keylen_in,add)                            \
do {                                                                             \
 unsigned _ha_bkt;                                                               \
 (add)->hh.key = (char*)(keyptr);                                                \
 (add)->hh.keylen = (unsigned)(keylen_in);                                       \
 if (!(head)) {                                                                  \
    head = (add);                                                                \
    HASH_MAKE_TABLE(tc, head, &((head)->hh));                                                    \
 }                                                                               \
 (head)->hh.tbl->num_items++;                                                    \
 (add)->hh.tbl = (head)->hh.tbl;                                                 \
 HASH_FCN(keyptr,keylen_in, (head)->hh.tbl->num_buckets,                         \
         (add)->hh.hashv, _ha_bkt, (head)->hh.tbl->log2_num_buckets);            \
 HASH_ADD_TO_BKT(tc, &((head)->hh.tbl->buckets[_ha_bkt]),&(add)->hh);  \
 HASH_FSCK(hh,head);                                                             \
} while(0)

#define HASH_ADD_KEYPTR_VM_STR(tc,hh,head,key_in,add)                            \
do {                                                                             \
 unsigned _ha_bkt;                                                               \
 MVMhashv cached_hash = (key_in)->body.cached_hash_code;                         \
 (add)->hh.key = (key_in);                                                       \
 if (!(head)) {                                                                  \
    head = (add);                                                                \
    HASH_MAKE_TABLE(tc, head, &((head)->hh));                                    \
 }                                                                               \
 (head)->hh.tbl->num_items++;                                                    \
 (add)->hh.tbl = (head)->hh.tbl;                                                 \
 if (cached_hash) {                                                              \
     (add)->hh.hashv = cached_hash;                                              \
     _ha_bkt = WHICH_BUCKET((cached_hash),                                       \
                  ((head)->hh.tbl->num_buckets),                                 \
        (head)->hh.tbl->log2_num_buckets);                                       \
 }                                                                               \
 else {                                                                          \
     HASH_FCN_VM_STR(tc, key_in, (head)->hh.tbl->num_buckets,                    \
             (add)->hh.hashv, _ha_bkt, (head)->hh.tbl->log2_num_buckets);        \
 }                                                                               \
 HASH_ADD_TO_BKT(tc, &((head)->hh.tbl->buckets[_ha_bkt]),&(add)->hh);            \
 HASH_FSCK(hh,head);                                                             \
} while(0)

#define HASH_TO_BKT( hashv, num_bkts, bkt, offset )                              \
do {                                                                             \
  bkt = WHICH_BUCKET((hashv), (num_bkts), (offset));                             \
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
#define HASH_DELETE(hh,head,delptr)                                              \
do {                                                                             \
    unsigned _hd_bkt;                                                            \
    struct UT_hash_handle *_hd_hh_del;                                           \
    if ( (head)->hh.tbl->num_items == 1 )  {                                     \
        uthash_free(tc, (head)->hh.tbl->buckets,                                     \
                    (head)->hh.tbl->num_buckets*sizeof(struct UT_hash_bucket) ); \
        uthash_free(tc, (head)->hh.tbl, sizeof(UT_hash_table));                      \
        (head) = NULL;                                                           \
    } else {                                                                     \
        _hd_hh_del = &((delptr)->hh);                                            \
        if ((delptr) == (head)) {                                                \
            unsigned cur = 0;                                                    \
            while (cur < (head)->hh.tbl->num_buckets) {                          \
                UT_hash_handle *cand = (head)->hh.tbl->buckets[cur].hh_head;     \
                while (cand) {                                                   \
                    if (cand && cand != &((delptr)->hh)) {                       \
                        DECLTYPE_ASSIGN((head), ELMT_FROM_HH((head)->hh.tbl,cand)); \
                        goto REPLACED_HEAD;                                      \
                    }                                                            \
                    cand = cand->hh_next;                                        \
                }                                                                \
                cur++;                                                           \
            }                                                                    \
            uthash_fatal("Failed to replace deleted head");                      \
          REPLACED_HEAD: ;                                                       \
        }                                                                        \
        _hd_bkt = WHICH_BUCKET( _hd_hh_del->hashv, (head)->hh.tbl->num_buckets, (head)->hh.tbl->log2_num_buckets);   \
        HASH_DEL_IN_BKT(&((head)->hh.tbl->buckets[_hd_bkt]), _hd_hh_del);        \
        (head)->hh.tbl->num_items--;                                             \
    }                                                                            \
    HASH_FSCK(hh,head);                                                          \
} while (0)

/* HASH_FSCK checks hash integrity on every add/delete when HASH_DEBUG is defined.
 * This is for uthash developer only; it compiles away if HASH_DEBUG isn't defined.
 */
#ifdef HASH_DEBUG
#define HASH_OOPS(...) do { fprintf(stderr,__VA_ARGS__); exit(-1); } while (0)
#define HASH_FSCK(hh,head)                                                       \
do {                                                                             \
    unsigned _bkt_i;                                                             \
    unsigned _count, _bkt_count;                                                 \
    char *_prev;                                                                 \
    struct UT_hash_handle *_thh;                                                 \
    if (head) {                                                                  \
        _count = 0;                                                              \
        for( _bkt_i = 0; _bkt_i < (head)->hh.tbl->num_buckets; _bkt_i++) {       \
            _bkt_count = 0;                                                      \
            _thh = (head)->hh.tbl->buckets[_bkt_i].hh_head;                      \
            _prev = NULL;                                                        \
            while (_thh) {                                                       \
               if (_prev != (char*)(_thh->hh_prev)) {                            \
                   HASH_OOPS("invalid hh_prev %p, actual %p\n",                  \
                    _thh->hh_prev, _prev );                                      \
               }                                                                 \
               _bkt_count++;                                                     \
               _prev = (char*)(_thh);                                            \
               _thh = _thh->hh_next;                                             \
            }                                                                    \
            _count += _bkt_count;                                                \
            if ((head)->hh.tbl->buckets[_bkt_i].count !=  _bkt_count) {          \
               HASH_OOPS("invalid bucket count %d, actual %d\n",                 \
                (head)->hh.tbl->buckets[_bkt_i].count, _bkt_count);              \
            }                                                                    \
        }                                                                        \
        if (_count != (head)->hh.tbl->num_items) {                               \
            HASH_OOPS("invalid hh item count %d, actual %d\n",                   \
                (head)->hh.tbl->num_items, _count );                             \
        }                                                                        \
    }                                                                            \
} while (0)
#else
#define HASH_FSCK(hh,head)
#endif

/* Use Jenkin's hash as the hash function. */
#define HASH_FCN HASH_JEN
#define HASH_FCN_VM_STR HASH_JEN_VM_STR

#define HASH_JEN_MIX(a,b,c)                                                      \
do {                                                                             \
  a -= b; a -= c; a ^= ( c >> 13 );                                              \
  b -= c; b -= a; b ^= ( a << 8 );                                               \
  c -= a; c -= b; c ^= ( b >> 13 );                                              \
  a -= b; a -= c; a ^= ( c >> 12 );                                              \
  b -= c; b -= a; b ^= ( a << 16 );                                              \
  c -= a; c -= b; c ^= ( b >> 5 );                                               \
  a -= b; a -= c; a ^= ( c >> 3 );                                               \
  b -= c; b -= a; b ^= ( a << 10 );                                              \
  c -= a; c -= b; c ^= ( b >> 15 );                                              \
} while (0)

#define HASH_JEN(key,keylen,num_bkts,hashv,bkt,offset)                           \
do {                                                                             \
  unsigned _hj_i,_hj_j,_hj_k;                                                    \
  unsigned char *_hj_key=(unsigned char*)(key);                                  \
  hashv = tc->instance->hashSecret;                                              \
  _hj_i = _hj_j = 0x9e3779b9;                                                    \
  _hj_k = (unsigned)(keylen);                                                    \
  while (_hj_k >= 12) {                                                          \
    _hj_i +=    (_hj_key[0] + ( (unsigned)_hj_key[1] << 8 )                      \
        + ( (unsigned)_hj_key[2] << 16 )                                         \
        + ( (unsigned)_hj_key[3] << 24 ) );                                      \
    _hj_j +=    (_hj_key[4] + ( (unsigned)_hj_key[5] << 8 )                      \
        + ( (unsigned)_hj_key[6] << 16 )                                         \
        + ( (unsigned)_hj_key[7] << 24 ) );                                      \
    hashv += (_hj_key[8] + ( (unsigned)_hj_key[9] << 8 )                         \
        + ( (unsigned)_hj_key[10] << 16 )                                        \
        + ( (unsigned)_hj_key[11] << 24 ) );                                     \
                                                                                 \
     HASH_JEN_MIX(_hj_i, _hj_j, hashv);                                          \
                                                                                 \
     _hj_key += 12;                                                              \
     _hj_k -= 12;                                                                \
  }                                                                              \
  hashv += keylen;                                                               \
  switch ( _hj_k ) {                                                             \
     case 11: hashv += ( (unsigned)_hj_key[10] << 24 );                          \
     case 10: hashv += ( (unsigned)_hj_key[9] << 16 );                           \
     case 9:  hashv += ( (unsigned)_hj_key[8] << 8 );                            \
     case 8:  _hj_j += ( (unsigned)_hj_key[7] << 24 );                           \
     case 7:  _hj_j += ( (unsigned)_hj_key[6] << 16 );                           \
     case 6:  _hj_j += ( (unsigned)_hj_key[5] << 8 );                            \
     case 5:  _hj_j += _hj_key[4];                                               \
     case 4:  _hj_i += ( (unsigned)_hj_key[3] << 24 );                           \
     case 3:  _hj_i += ( (unsigned)_hj_key[2] << 16 );                           \
     case 2:  _hj_i += ( (unsigned)_hj_key[1] << 8 );                            \
     case 1:  _hj_i += _hj_key[0];                                               \
  }                                                                              \
  HASH_JEN_MIX(_hj_i, _hj_j, hashv);                                             \
  if (hashv == 0) {                                                              \
      hashv += keylen;                                                           \
  }                                                                              \
  bkt = WHICH_BUCKET(hashv, num_bkts, offset);                                   \
} while(0)

#define HASH_JEN_VM_STR(tc,key,num_bkts,hashv,bkt,offset)                        \
do {                                                                             \
  MVM_string_compute_hash_code(tc, key);                                         \
  hashv = (key)->body.cached_hash_code;                                          \
  bkt = WHICH_BUCKET((hashv), (num_bkts), offset);                               \
} while(0)

/* key comparison function; return 0 if keys equal */
#define HASH_KEYCMP(a,b,len) memcmp(a,b,len)

/* iterate over items in a known bucket to find desired item */
#define HASH_FIND_IN_BKT(tbl,hh,head,keyptr,keylen_in,out, hashval)              \
do {                                                                             \
 if (head.hh_head) DECLTYPE_ASSIGN(out,ELMT_FROM_HH(tbl,head.hh_head));          \
 else out=NULL;                                                                  \
 while (out) {                                                                   \
    if ((out)->hh.hashv == hashval && (out)->hh.keylen == keylen_in) {                                           \
        if ((HASH_KEYCMP((out)->hh.key,keyptr,keylen_in)) == 0) break;             \
    }                                                                            \
    if ((out)->hh.hh_next) DECLTYPE_ASSIGN(out,ELMT_FROM_HH(tbl,(out)->hh.hh_next)); \
    else out = NULL;                                                             \
 }                                                                               \
} while(0)

/* iterate over items in a known bucket to find desired item */
#define HASH_FIND_IN_BKT_VM_STR(tc,tbl,hh,head,key_in,out,hashval)               \
do {                                                                             \
 if (head.hh_head) DECLTYPE_ASSIGN(out,ELMT_FROM_HH(tbl,head.hh_head));          \
 else out=NULL;                                                                  \
 while (out) {                                                                   \
    if (hashval == (out)->hh.hashv && MVM_string_equal(tc, (key_in), (MVMString *)((out)->hh.key)))            \
        break;                                                                   \
    if ((out)->hh.hh_next)                                                       \
        DECLTYPE_ASSIGN(out,ELMT_FROM_HH(tbl,(out)->hh.hh_next));                \
    else                                                                         \
        out = NULL;                                                              \
 }                                                                               \
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
    unsigned _he_bkt;
    unsigned _he_bkt_i;
    struct UT_hash_handle *_he_thh, *_he_hh_nxt;
    UT_hash_bucket *_he_new_buckets, *_he_newbkt;
    unsigned new_num_bkts = tbl->num_buckets * 2;
    unsigned new_log2_num_buckets = tbl->log2_num_buckets + 1;
    _he_new_buckets = (UT_hash_bucket*)uthash_malloc_zeroed(tc,
             new_num_bkts * sizeof(struct UT_hash_bucket));
    tbl->ideal_chain_maxlen =
       (tbl->num_items >> new_log2_num_buckets) +
       ((tbl->num_items & (new_num_bkts-1)) ? 1 : 0);
    tbl->nonideal_items = 0;
    /* Iterate the buckets */
    for(_he_bkt_i = 0; _he_bkt_i < tbl->num_buckets; _he_bkt_i++)
    {
        _he_thh = tbl->buckets[ _he_bkt_i ].hh_head;
        /* Iterate items in the bucket */
        while (_he_thh) {
           _he_hh_nxt = _he_thh->hh_next;
           _he_bkt = WHICH_BUCKET( _he_thh->hashv, new_num_bkts, new_log2_num_buckets);
           _he_newbkt = &(_he_new_buckets[ _he_bkt ]);
           if (++(_he_newbkt->count) > tbl->ideal_chain_maxlen) {
             tbl->nonideal_items++;
             _he_newbkt->expand_mult = _he_newbkt->count /
                                        tbl->ideal_chain_maxlen;
           }
           _he_thh->hh_prev = NULL;
           _he_thh->hh_next = _he_newbkt->hh_head;
           if (_he_newbkt->hh_head) _he_newbkt->hh_head->hh_prev =
                _he_thh;
           _he_newbkt->hh_head = _he_thh;
           _he_thh = _he_hh_nxt;
        }
    }
    uthash_free(tc, tbl->buckets, tbl->num_buckets*sizeof(struct UT_hash_bucket) );
    tbl->num_buckets = new_num_bkts;
    tbl->log2_num_buckets = new_log2_num_buckets;
    tbl->buckets = _he_new_buckets;
    tbl->ineff_expands = (tbl->nonideal_items > (tbl->num_items >> 1)) ?
        (tbl->ineff_expands+1) : 0;
    if (tbl->ineff_expands > 1) {
        tbl->noexpand=1;
        uthash_noexpand_fyi(tbl);
    }
    uthash_expand_fyi(tbl);
}

/* add an item to a bucket  */
MVM_STATIC_INLINE void HASH_ADD_TO_BKT(MVMThreadContext *tc, UT_hash_bucket *head, UT_hash_handle *addhh) {
 head->count++;
 addhh->hh_next = head->hh_head;
 addhh->hh_prev = NULL;
 if (head->hh_head) { head->hh_head->hh_prev = addhh; }
 head->hh_head = addhh;
 if (head->count >= ((head->expand_mult+1) * HASH_BKT_CAPACITY_THRESH)
     && addhh->tbl->noexpand != 1) {
        HASH_EXPAND_BUCKETS(tc, addhh->tbl);
 }
}

/* remove an item from a given bucket */
MVM_STATIC_INLINE void HASH_DEL_IN_BKT(UT_hash_bucket *head, UT_hash_handle *hh_del) {
    head->count--;
    if (head->hh_head == hh_del) {
      head->hh_head = hh_del->hh_next;
    }
    if (hh_del->hh_prev) {
        hh_del->hh_prev->hh_next = hh_del->hh_next;
    }
    if (hh_del->hh_next) {
        hh_del->hh_next->hh_prev = hh_del->hh_prev;
    }
}

#define HASH_CLEAR(tc, hh,head)                                                  \
do {                                                                             \
  if (head) {                                                                    \
    uthash_free(tc, (head)->hh.tbl->buckets,                                     \
                (head)->hh.tbl->num_buckets*sizeof(struct UT_hash_bucket));      \
    uthash_free(tc, (head)->hh.tbl, sizeof(UT_hash_table));                      \
    (head)=NULL;                                                                 \
  }                                                                              \
} while(0)

/* obtain a count of items in the hash */
#define HASH_CNT(hh,head) ((head)?((head)->hh.tbl->num_items):0)

MVM_STATIC_INLINE void * HASH_ITER_FIRST_ITEM(
        struct UT_hash_table *ht, unsigned *bucket_tmp) {
    if (!ht)
        return NULL;
    while (*bucket_tmp < ht->num_buckets) {
        struct UT_hash_handle *hh_head = ht->buckets[*bucket_tmp].hh_head;
        if (hh_head)
            return ELMT_FROM_HH(ht, hh_head);
        (*bucket_tmp)++;
    }
    return NULL;
}
MVM_STATIC_INLINE void * HASH_ITER_NEXT_ITEM(
        struct UT_hash_handle *cur_handle, unsigned *bucket_tmp) {
    struct UT_hash_table *ht = cur_handle->tbl;
    if (cur_handle->hh_next)
        return ELMT_FROM_HH(ht, cur_handle->hh_next);
    (*bucket_tmp)++;
    while (*bucket_tmp < ht->num_buckets) {
        struct UT_hash_handle *hh_head = ht->buckets[*bucket_tmp].hh_head;
        if (hh_head)
            return ELMT_FROM_HH(ht, hh_head);
        (*bucket_tmp)++;
    }
    return NULL;
}
#define HASH_ITER(hh,head,el,tmp,bucket_tmp)                                    \
for((bucket_tmp) = 0,                                                           \
    (el) = HASH_ITER_FIRST_ITEM((head) ? (head)->hh.tbl : NULL, &(bucket_tmp)), \
    (tmp) = ((el) ? HASH_ITER_NEXT_ITEM(&((el)->hh), &(bucket_tmp)) : NULL);    \
    (el);                                                                       \
    (el) = (tmp),                                                               \
    (tmp) = ((tmp) ? HASH_ITER_NEXT_ITEM(&((tmp)->hh), &(bucket_tmp)) : NULL))

#endif /* UTHASH_H */
