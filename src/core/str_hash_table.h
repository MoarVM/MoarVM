/* A Better Hash.

A C implementation of https://github.com/martinus/robin-hood-hashing
by Martin Ankerl <http://martin.ankerl.com>

Better than what we had. Not better than his. His is hard to beat.

His design is for a Robin Hood hash (ie open addressing, Robin Hood probing)
with:

* a contiguous block of memory
* hash into 2**n slots
* instead of wrapping round from the end to the start of the array when
  probing, *actually* allocate some extra slots at the end, sufficient to cover
  the maximum permitted probe length
* store metadata for free/used (with the offset from the ideal slot) in a byte
  array immediately after the data slots
* store the offset in the top n bits of the byte, use the lower 8-n bits
  (possibly 0) to store (more) bits of the key's hash in the rest.
  (where n might be 0 - n is updated dynamically whenever a probe would overflow
   the currently permitted maximum)
  (so m bits of the hash are used to pick the ideal slot, and a different n are
   in the metadata, meaning that misses can be rejected more often)
* sentinel byte at the end of the metadata to cause the iterator to efficiently
  terminate
* setting max_items to 0 to force a resize before even trying another allocation
* when inserting and stealing a slot, move the next items up in bulk
  (ie don't implement it as "swap the new element with the evicted element and
  carry on inserting - the rest of the elements are already in a valid probe
  order, so just update *all* their metadata bytes, and then memmove them)

it's incredibly flexible (up to, automatically choosing whether to allocate
the value object inline in the hash, or indrected via a pointer), but
implemented as a C++ template.

Whereas we need something in C. Only for small structures, so they can always
go inline. And it turns out, our keys are always pointers, and easily "hashed"
(either because they are, because they point to something that caches its
hash value, or because we fake it and explicitly store the hash value.)

Some tuning is still possible (and probably a good idea), but the only
significant code optimisation not implemented would be to change iterator
metadata processing to be word-at-a-time when possible. And even that might not
be worth it.

*/

/* As to hash randomisation.
 *
 * The key thing about "Denial of Service via Algorithmic Complexity Attacks"
 * https://www.usenix.org/conference/12th-usenix-security-symposium/denial-service-algorithmic-complexity-attacks
 *
 * is that the attacker wins if she is able to cause the same effects as a brute
 * force attack but for much less work. So "all" we have to do is to ensure that
 * it takes as much effort to figure out how to bypass our mitigations as it
 * does just to brute force attack us. ie - deny the *shortcut*.
 *
 * The weakness of "classic" hash tables prior to this paper is that the
 * function that maps keys to buckets is constant, so the attacker can
 * pre-compute keys that will induce linear behaviour.
 *
 * The "classic" fix to that was to introduce some sort of randomisation of the
 * hash mapping (often described as a "seed", but probably better described as a
 * "salt"), so that pre-computing keys no longer works.
 *
 * The problem with implementations of this (including ours) is that for
 * efficiency reasons implementations like to have the hash function be the same
 * for all hash tables (within a process). Implying that there is one process
 * global salt.
 * In turn, this meant that it became possible for a more determined attacker
 * to remotely probe and determine the salt through information leakage -
 * typically by the iteration order of hash tables, and this being exposed.
 * In particular, bucket chains and bucket splitting meant that one bit of the
 * hash value would determine how keys on a single chain were split, and hence
 * which order they would appear in when a hash doubled in size.
 *
 * So in turn, the fix for *that* was often to obfuscate the iteration order.
 * Usually on a per-hash table basis.
 *
 * Previously we did this by generating a pseudo random number for each hash,
 * and using it to perturb the order of bucket chains. As explained:
 *
 * Get a pseudo-random bucket. This works because XORing a random x bit integer
 * with 0..(2**x)-1 will give you 0..(2**x)-1 in a pseudo random order (not *really*
 * random but random enough for our purposes. Example with 0..(2**3)-1 and the rand int is 3
 * 0 ^ 3 = 3; 1 ^ 3 = 2;
 * 2 ^ 3 = 1; 3 ^ 3 = 0;
 * 4 ^ 3 = 7; 5 ^ 3 = 6;
 * 6 ^ 3 = 5; 7 ^ 3 = 4;
 *
 *
 * I realise that can actually take a different approach. Instead of perturbing
 * the bucket order on *iteration*, we can perturb it on insertion. This ends up
 * with the same end result - effectively we store the buckets in a shuffled
 * order and iterate them linearly, instead of storing the buckets in a linear
 * order and iterating them shuffled.
 * We then also pick a new salt for each size doubling, meaning that *all* bits
 * of the hash value contribute to the order change on size doubling (which can
 * be observed by an attacker), not just one.
 *
 * This should be just as secure, but faster. Now prove me wrong with a working
 * attack, and preferably suggest a better solution. :-)
 * (As you can see from "in turn", people were wrong before.)
 */

/* Conceptually, the design is this
 *
 *  Control            Entries
 *  structure
 *
 * +-------------+    +----------------+----------------+----------------+----
 * |             |    | probe distance | probe distance | probe distance |
 * | entries     | -> | key            | key            | key            | ...
 * | other stuff |    | value          | value          | value          |
 * +-------------+    +----------------+----------------+----------------+----
 *
 * but as probe distance is one byte, and key is a pointer, this would waste a
 * lot of memory due to alignment padding, so the design actually ends up as
 * this, with probe distance stored separately a byte array, `metadata`.
 *
 * Also, "conceptually"...
 * We are using open addressing. What this means is that, whilst each hash entry
 * has an "ideal" location in the array, if that location is already in use, we
 * put the entry "nearby". With "Robin Hood" hashing, "nearby" is a location
 * "soon" after (with logic for re-ordering entries), but the upshot of this is
 * that the "actual" position is "ideal" + "probe distance", and that value has
 * to be wrapped (modulo the array size) to find the actual bucket location.
 *
 * So modulo approach means that a hash of size 8 looks like this:
 *
 * +-------------+    +---+---+---+---+---+---+---+---+
 * | metadata    | -> | a | b | c | d | e | f | g | h |  probe distances
 * |             |    +---+---+---+---+---+---+---+---+
 * |             |
 * |             |    +---+---+---+---+---+---+---+---+
 * | entries     | -> | A | B | C | D | E | F | G | H |  key, value
 * | other stuff |    +---+---+---+---+---+---+---+---+
 * +-------------+
 *
 * whereas what we actually do is "unwrap" the modulo, and allocate the worst
 * case extra array slots at the end (longest possible probe distance, starting
 * at the last "official" entry). So for an array of size 8, load factor of
 * 0.75 the longest probe distance is 5 (when all 6 entries would ideally be in
 * the last bucket), so what we actually have is this
 *
 * +----------+        +---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * | metadata | ->     | a | b | c | d | e | f | g | h | i | j | k | l | m | 0 |
 * |          |        +---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * | (other)  |
 * |          |        +---+---+---+---+---+---+---+---+---+---+---+---+---+
 * | entries  | ->     | A | B | C | D | E | F | G | H | I | J | K | L | M |
 * +----------+        +---+---+---+---+---+---+---+---+---+---+---+---+---+
 *
 *                     <-- official bucket positions --><--   overflow   -->
 *
 * We include a sentinel value at the end of the metadata so that the probe
 * distance loop doesn't need a bounds check. We *had* allocated an extra byte
 * at the start too, to make the pointer arithmetic work, but that isn't needed
 * now that we use a single memory block.
 *
 * Finally, to reduce allocations and keep things in the same CPU cache lines,
 * what we allocate in memory actually looks like this:
 *
 * ---+---+---+---+---+---+---+---+---------+---+---+---+---+---+---+---+---
 * ...| G | F | E | D | C | B | A | control | a | b | c | d | e | f | g |...
 * ---+---+---+---+---+---+---+---+---------+---+---+---+---+---+---+---+---
 *                                ^
 *                                |
 *                              +---+
 * the public MVMStrHashTable   |   |
 *                              +---+
 *
 * is just a pointer to the dynamically allocated structure.
 *
 * This layout means that a hash clone is
 * 1) malloc
 * 2) memcpy
 * 3) fix up the GC invariants
 */

/* How does storing hash bits in the metadata work? And how come there doesn't
 * seem to be *any* explicit code for testing it in the fetch loop?
 *
 * Consider the 6 keys 'R', 'a', 'k', 'u' 'd' 'o'.
 * We'll assume that the hash value for each is their ASCII code.
 * Assuming a hash with 16 buckets, we use the top 4 bits of the hash value
 * to determine the ideal bucket for the key - the bucket that the key would go
 * in, if inserted into an empty hash. So our keys look like this:
 *
 *           hash   bucket   extra
 * R     01010010        5       2
 * a     01100001        6       1
 * k     01101011        6      11
 * u     01110101        7       5
 * d     01100100        6       8
 * o     01101111        6      15
 *      /   ||   \
 *     bucket extra
 *
 * If we insert them in that order into the hash, then they would be laid out
 * like this:
 *
 *     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *     | A | B | C | D | E | F | G | H | I | J | K | L | M | N | O | P | ...
 *     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * key                       R   a   k   d   o   u
 * probe distance            1   1   2   3   4   4
 *
 *
 * Insert order doesn't matter - this order would be equally valid and would be
 * what we get by inserting the 6 keys in reverse order:
 *
 *     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *     | A | B | C | D | E | F | G | H | I | J | K | L | M | N | O | P | ...
 *     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * key                       R   o   d   k   a   u
 * probe distance            1   1   2   3   4   4
 *
 * Consider the search for the key 'u'. Its ideal bucket is 7. We start with a
 * probe distance of one. *Only* metadata bytes that equal the probe distance
 * might be matches - we can immediately dismiss all other entries as "can't
 * match" without even having to look up their full entry to get the key. So for
 * 'u', we have:
 *
 *                                   ^
 *                                  != 0x01 (so can't match)
 *                                  >= 0x01 (so keep going)
 *
 *                                       ^
 *                                      != 0x02 (so can't match)
 *                                      >= 0x02 (so keep going)
 *
 *                                           ^
 *                                          != 0x03 (so can't match)
 *                                          >= 0x03 (so keep going)
 *
 *                                                   ^
 *                                              == 0x04 (so might match match)
 *                                              ('u' eq 'u'; found; return)
 *
 *
 * Note that we don't even look at the "main" hash entries until the last case -
 * we are in a tight loop with small data hot in the cache. Long probe distances
 * aren't themselves aren't terrible - what hurts more is when multiple hash
 * entries all contest the same ideal bucket. A search for 'k' would find it at
 * probe distance 3, but would have had to look up and test the keys at probe
 * distances 1 and 2 before then.
 *
 * The trick in the design from Martin Ankerl is to also store some *more* bits
 * from the hash value in the metadata byte. Remember, the Robin Hood
 * invariant is only about probe distance. So this order is *also* valid:
 *
 *     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *     | A | B | C | D | E | F | G | H | I | J | K | L | M | N | O | P | ...
 *     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * key                       R   a   d   k   o   u
 * probe distance            1   1   2   3   4   4
 *
 * Consider the extra hash bits for each key:
 *
 *                           2   1   8  11  15   5
 *
 * if we store the metadata as (probe distance) << 4 | (extra hash bits) then it
 * is (in hexadecimal) laid out like this:
 *
 *                          12  11  28  3b  4f  45
 *
 * Consider the search for the key 'k'. Its ideal bucket is 6. Its extra hash
 * bits are 11. So we start with bucket 6, metadata byte 0x1b. Each time we
 * advance the bucket, we add (1 << 4) to the metadata byte we need to find to
 * match. So:
 *
 *     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *     | A | B | C | D | E | F | G | H | I | J | K | L | M | N | O | P | ...
 *     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * key                       R   a   d   k   o   u
 * metadata byte            12  11  28  3b  4f  45
 *                               ^
 *                              != 0x1b (so can't match)
 *                              >= 0x1b (so keep going)
 *
 *                                   ^
 *                                  != 0x2b (so can't match)
 *                                  >= 0x2b (so keep going)
 *
 *                                       ^
 *                                      == 0x3b (so might match)
 *                                      ('k' eq 'k', so found; return)
 *
 * Notice that we didn't even have to do the string comparison for 'k' != 'a'
 * and for 'k' != 'd'. They fall out naturally from the existing byte
 * comparisons, and how the metadata is stored.
 *
 * For lookup we have had to do a little more CPU work to calculate the metadata
 * byte we expect to see (figuring out the increment, rather than it just being
 * +1 each time) but we hope that pays off by doing less work on key comparisons
 * (and fewer cache misses)
 *
 *
 * Finally, how "not found" works. In this case, the extra hash bits don't give
 * a shortcut. Say we search for 'p'. Its ideal bucket is also 6. Its extra hash
 * bits are 10. So again we start with bucket 6, but metadata byte 0x1a.
 *
 * The first 2 steps are the same. At the third, we diverge:
 *
 *                                       ^
 *                                      != 0x3a (so can't match)
 *                                      >= 0x3a (so keep going)
 *
 *                                           ^
 *                                          != 0x4a (so can't match)
 *                                          >= 0x4a (so keep going)
 *
 *                                                   ^
 *                                              != 0x5a (so can't match)
 *                                              <  0x5a (so not found; return)
 *
 *
 * There is, however, a trade off. For the examples at the top, without the
 * extra hash bits, by choosing to always insert later, we minimise the amount
 * we move.  The hash fills up like this:
 *
 * key                         R
 * probe distance              1
 *
 * key                         R   a
 * probe distance              1   1
 *
 * key                         R   a   k
 * probe distance              1   1   2
 *
 * key                         R   a   k   u
 * probe distance              1   1   2   2
 * [move 'u' to insert 'd']
 * key                         R   a   k   d   u
 * probe distance              1   1   2   3   3
 * [move 'u' to insert 'o']
 * key                         R   a   k   d   o   u
 * probe distance              1   1   2   3   4   4
 *
 *
 * But with hash bits in the metadata, to maintain the order, the hash fills up
 * like this
 *
 * key                         R
 * probe distance              1
 *
 * key                         R   a
 * probe distance              1   1
 *
 * key                         R   a   k
 * probe distance              1   1   2
 *
 * key                         R   a   k   u
 * probe distance              1   1   2   2
 * [move 'k', 'u' to insert 'd']
 * key                         R   a   d   k   u
 * probe distance              1   1   2   3   3
 * [move 'u' to insert 'o']
 * key                         R   a   d   k   o   u
 * probe distance              1   1   2   3   4   4
 *
 * More entries had to be moved around. That's more CPU and more cache churn.
 *
 * So there is a definite trade off here. There's more work creating hashes,
 * with the hoped-for trade off that there will be less work for hash lookups.
 *
 * Note also that putting probe distance in the higher bits is deliberate. It
 * means that we can easily process the metadata bytes to increase the number of
 * bits used to store probe distance *without* breaking either the Robin Hood
 * invariant, or the "extra bits" invariant/assumption. So if we had (4, 4), we
 * can go to (5, 3) with a bitshift:
 *
 * key                  R        a        d        k        o        u
 * metadata was  00010010 00010001 00101000 00111011 01001111 01000101
 *                  1   2    1   1    2   8    3   b    4   f    4   5
 * metadata now  00001001 00001000 00010100 00011101 00100111 00100010
 *                   1  1     1  0     2  4     3  5     4  7     4  2
 *
 * which we can do word-at-a-time with a suitable mask (0x7f7f7f7f7f7f7f7f)
 * and without re-ordering *anything*.
 *
 * Note for tuning this
 *
 * 1) we don't *have* to start out by using 4 (or even 5) bits for metadata. We
 *    could use 3. Fewer bits *increases* the chance of needing to do a key
 *    lookup, but *decreases* the amount of memmove during hash inserts.
 *
 * 2) Higher load factor means longer probe distances, but also makes more
 *    metadata fit in the CPU cache. It also means more work on insert and
 *    delete.
 *
 * 3) Currently we grow the hash when we exceed the load factor *or* when we run
 *    out of bits to store the probe distance. We could choose to limit the
 *    probe distance, and grow the hash sooner if we get bad luck with probing.
 *
 * 4) We could pick both the load factors and the hash metadata split
 *    differently at different hash sizes (or if the hash size was given at
 *    build time).
 *
 * 5) see the comment in str_hash_table_funcs.h about (not) checking the key's
 *    full 64 hash value
 *
 * The metadata lookup loop is pretty tight. For arm32, it's 8 instructions,
 * for each metadata "miss".
 *
 * The hash bits in metadata doesn't bring us as much speedup as I had hoped. I
 * *think* that this is because (for us) the full key lookup is *relatively*
 * cheap - it's two pointer dereferences and an equality test that will usually
 * "fail fast". There might still be a win here, but it might be hard to be sure
 * of, and it might only be a CPU for "last level cache miss" trade off.
 */

struct MVMStrHashTableControl {
    MVMuint64 salt;
#if HASH_DEBUG_ITER
    MVMuint64 ht_id;
    MVMuint32 serial;
    MVMuint32 last_delete_at;
#endif
    /* If cur_items and max_items are *both* 0 then we only allocated a control
     * structure. All of the other entries in the struct are bogus, apart from
     * entry_size, and calling many of the accessor methods for the hash will
     * fail assertions.
     * ("Doctor, Doctor, it hurts when I do this". "Well, don't do that then.")
     * Iterators will return end immediately, fetch will fast track a not-found
     * result, and insert will immediately allocate the default minimum size. */
    MVMHashNumItems cur_items;
    MVMHashNumItems max_items; /* hit this and we grow */
    MVMuint8 official_size_log2;
    MVMuint8 key_right_shift;
    MVMuint8 entry_size;
    /* This is the maximum probe distance we can use without updating the
     * metadata. It might not *yet* be the maximum probe distance possible for
     * the official_size. */
    MVMuint8 max_probe_distance;
    /* This is the maximum probe distance possible for the official size.
     * We can (re)calcuate this from other values in the struct, but it's easier
     * to cache it as we have the space. */
    MVMuint8 max_probe_distance_limit;
    MVMuint8 metadata_hash_bits;
    /* This is set to 0 when the control structure is allocated. When the hash
     * expands (and needs a new larger allocation) this is set to 1 in the
     * soon-to-be-freed memory, and the memory is scheduled to be released at
     * the next safe point. This way avoid C-level use-after-free if threads
     * attempt to mutate the same hash concurrently, and hopefully can spot at
     * least some cases and fault them, often enough for bugs to be noticed. */
    volatile MVMuint8 stale;
};

struct MVMStrHashTable {
    struct MVMStrHashTableControl *table;
};

struct MVMStrHashHandle {
    MVMString *key;
};

typedef struct {
    MVMuint32 pos;
#if HASH_DEBUG_ITER
    MVMuint32 serial;
    MVMuint64 owner;
#endif
}  MVMStrHashIterator;

MVMuint32 MVM_round_up_log_base2(MVMuint32 v);

#if HASH_DEBUG_ITER
MVM_STATIC_INLINE int MVM_str_hash_iterator_target_deleted(MVMThreadContext *tc,
                                                           MVMStrHashTable *hashtable,
                                                           MVMStrHashIterator iterator) {
    /* Returns true if the hash entry that the iterator points to has been
     * deleted (and this is the only action on the hash since the iterator was
     * created) */
    struct MVMStrHashTableControl *control = hashtable->table;
    if (MVM_UNLIKELY(control && control->stale)) {
        MVM_oops(tc, "MVM_str_hash_iterator_target_deleted called with a stale hashtable pointer");
    }
    return control && iterator.serial == control->serial - 1 &&
        iterator.pos == control->last_delete_at;
}
#endif

/* So why is this here, instead of _funcs?
 * Because it is needed in MVM_iter_istrue_hash, which is inline in MVMIter.h
 * So this definition has to be before that definition.
 * In turn, various other inline functions in the reprs are used in
 * str_hash_table_funcs.h, so those declarations have to be seen already, and
 * as the reprs headers are included as one block, *most* of the MVMStrHashTable
 * functions need to be later. */

MVM_STATIC_INLINE int MVM_str_hash_at_end(MVMThreadContext *tc,
                                           MVMStrHashTable *hashtable,
                                           MVMStrHashIterator iterator) {
    if (MVM_UNLIKELY(hashtable->table && hashtable->table->stale)) {
        MVM_oops(tc, "MVM_str_hash_at_end called with a stale hashtable pointer");
    }
#if HASH_DEBUG_ITER
    struct MVMStrHashTableControl *control = hashtable->table;
    MVMuint64 ht_id = control ? control->ht_id : 0;
    if (iterator.owner != ht_id) {
        MVM_oops(tc, "MVM_str_hash_at_end called with an iterator from a different hash table: %016" PRIx64 " != %016" PRIx64,
                 iterator.owner, ht_id);
    }
    MVMuint32 serial = control ? control->serial : 0;
    if (iterator.serial != serial
        || MVM_str_hash_iterator_target_deleted(tc, hashtable, iterator)) {
        MVM_oops(tc, "MVM_str_hash_at_end called with an iterator with the wrong serial number: %u != %u",
                 iterator.serial, serial);
    }
#endif
    return iterator.pos == 0;
}
