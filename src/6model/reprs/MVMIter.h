/* Representation used by VM-level iterators. */

#define MVM_ITER_MODE_ARRAY         0
#define MVM_ITER_MODE_ARRAY_INT     1
#define MVM_ITER_MODE_ARRAY_NUM     2
#define MVM_ITER_MODE_ARRAY_STR     3
#define MVM_ITER_MODE_HASH          4

struct MVMIterBody {
    /* whether hash or array */
    MVMuint32 mode;

    /* array or hash being iterated */
    MVMObject *target;

    /* next hash item to give or next array index */
    union {
        struct {
            MVMStrHashIterator curr;
            MVMStrHashIterator next;
        } hash_state;
        struct {
            MVMint64 index;
            MVMint64 limit;
        } array_state;
    };
};
struct MVMIter {
    MVMObject common;
    MVMIterBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMIter_initialize(MVMThreadContext *tc);

MVMObject * MVM_iter(MVMThreadContext *tc, MVMObject *target);
MVMint64 MVM_iter_istrue(MVMThreadContext *tc, MVMIter *iter);
MVMString * MVM_iterkey_s(MVMThreadContext *tc, MVMIter *iterator);
MVMObject * MVM_iterval(MVMThreadContext *tc, MVMIter *iterator);

MVM_STATIC_INLINE MVMint64 MVM_iter_istrue_array(MVMThreadContext *tc, MVMIter *iterator) {
    return iterator->body.array_state.index + 1 < iterator->body.array_state.limit ? 1 : 0;
}

MVM_STATIC_INLINE MVMint64 MVM_iter_istrue_hash(MVMThreadContext *tc, MVMIter *iterator) {
    MVMIterBody *body = &iterator->body;
    MVMStrHashTable *hashtable = &(((MVMHash *)body->target)->body.hashtable);

#if HASH_DEBUG_ITER
    struct MVMStrHashTableControl *control = hashtable->table;
    MVMuint64 ht_id = control ? control->ht_id : 0;
    if (body->hash_state.curr.owner != ht_id) {
        MVM_oops(tc, "MVM_iter_istruehash called with an iterator from a different hash table: %016" PRIx64 " != %016" PRIx64,
                 body->hash_state.curr.owner, control ? control->ht_id : 0);
    }
    /* OK, to implement "delete at current iterator position" we need
     * to cheat somewhat. */
    if (MVM_str_hash_iterator_target_deleted(tc, hashtable, body->hash_state.curr)) {
        /* The only action taken on the hash was to delete at the
         * current iterator. In which case, the "next" iterator is
         * valid (but has already been advanced beyond pos, so we
         * can't perform this test on it. So "fix up" its state to pass
         * muster with the HASH_DEBUG_ITER sanity tests. */
        body->hash_state.next.serial = control->serial;
    }
#endif

    return MVM_str_hash_at_end(tc, hashtable, body->hash_state.next) ? 0 : 1;
}
