/* At the moment this is a massively revised version of uthash.h, hence: */

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

/* The plan is (roughly)
   0) Turn uthash "the right way out" (without randomisation)
   1) Splitting the implementation into 3 or 4 distinct hash types based on the
      key used.
   2) Replace "bind" with something more like "store"
   3) And the allocation for hash entries is done within the hash
      (needed to migrate to open addressing)
   4) Make sure that the C interface can easily wrap the interface provided
      by the C++ std::unordered_map
   5) on a development-only branch actually wrap std::unordered_map
   6) and then replace that with the faster templates elsewhere on the Internet
   7) once proven, and an implementation chosen, transcribe that to C
   8) add randomisation back
      Probably using the same approach of XOR-a-constant during iteration.
      http://burtleburtle.net/bob/rand/isaac.html
      looks interesting as a viable RNG for this.
*/

struct MVMFixKeyHashBucket {
    struct MVMFixKeyHashHandle *hh_head;
    MVMHashNumItems count;

    /* expand_mult is normally set to 0. In this situation, the max chain length
     * threshold is enforced at its default value, HASH_BKT_CAPACITY_THRESH. (If
     * the bucket's chain exceeds this length, bucket expansion is triggered).
     * However, setting expand_mult to a non-zero value delays bucket expansion
     * (that would be triggered by additions to this particular bucket)
     * until its chain length reaches a *multiple* of HASH_BKT_CAPACITY_THRESH.
     * (The multiplier is simply expand_mult+1). The whole idea of this
     * multiplier is to reduce bucket expansions, since they are expensive, in
     * situations where we know that a particular bucket tends to be overused.
     * It is better to let its chain length grow to a longer yet-still-bounded
     * value, than to do an O(n) bucket expansion too often.
     */
    MVMHashUInt expand_mult;
};

/* Some of the types used here are not optimal. But this code will go soon
   (see above) so it's KISS & YAGNI (because really, you aren't). */

struct MVMFixKeyHashTable {
    struct MVMFixKeyHashBucket *buckets;
    MVMHashBktNum num_buckets;
    MVMHashUInt log2_num_buckets;
    MVMHashNumItems num_items;

    /* in an ideal situation (all buckets used equally), no bucket would have
     * more than ceil(#items/#buckets) items. that's the ideal chain length. */
    MVMHashUInt ideal_chain_maxlen;

    /* nonideal_items is the number of items in the hash whose chain position
     * exceeds the ideal chain maxlen. these items pay the penalty for an uneven
     * hash distribution; reaching them in a chain traversal takes >ideal steps */
    MVMHashUInt nonideal_items;

    /* ineffective expands occur when a bucket doubling was performed, but
     * afterward, more than half the items in the hash had nonideal chain
     * positions. If this happens on two consecutive expansions we inhibit any
     * further expansion, as it's not helping; this happens when the hash
     * function isn't a good fit for the key domain. When expansion is inhibited
     * the hash will still work, albeit no longer in constant time. */
    MVMHashUInt ineff_expands;
    MVMHashUInt noexpand;

    /* The size of the hash entry. Includes the MVMString *. Must be set
     * before you can store anything in the hash.
     * Effectively, if you set this to sizeof(MVMString *) you have a set.
     */
    MVMHashUInt entry_size;
};

/* Indirects to your struct, which must start with an MVMString *
   Your struct must be allocated from the FSA (see kv_size above)
   because in the future the allocation will be internal (see the plan)
*/

struct MVMFixKeyHashHandle {
    MVMString **key;
    struct MVMFixKeyHashHandle *hh_next;   /* next hh in bucket order        */
};
