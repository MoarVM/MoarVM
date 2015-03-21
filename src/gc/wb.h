/* Functions for if the write barriers are hit. */
MVM_PUBLIC void MVM_gc_write_barrier_hit(MVMThreadContext *tc, MVMCollectable *update_root);

/* Ensures that if a generation 2 object comes to hold a reference to a
 * nursery object, then the generation 2 object becomes an inter-generational
 * root. */
MVM_STATIC_INLINE void MVM_gc_write_barrier(MVMThreadContext *tc, MVMCollectable *update_root, const MVMCollectable *referenced) {
    if (((update_root->flags & MVM_CF_SECOND_GEN) && referenced && !(referenced->flags & MVM_CF_SECOND_GEN)))
        MVM_gc_write_barrier_hit(tc, update_root);
}

/* Does an assignment, but makes sure the write barrier MVM_WB is applied
 * first. Takes the root object, the address within it we're writing to, and
 * the thing we're writing. Note that update_addr is not involved in the
 * write barrier. */
#define MVM_ASSIGN_REF(tc, update_root, update_addr, referenced) \
    { \
        void *_r = referenced; \
        MVM_gc_write_barrier(tc, update_root, (MVMCollectable *)_r); \
        update_addr = _r; \
    }

/* Unconditional frame lexical write barrier. It's always safe to clear the
 * "only references gen2" flag even if it's an overestimate, and we can put
 * it back in place next GC if we're wrong about needing it. This is used if
 * we don't cheaply know if we're writing to an object lexical. */
MVM_STATIC_INLINE void MVM_gc_frame_lexical_write_barrier_unc(MVMThreadContext *tc, MVMFrame *f) {
    f->refs_gen2_only = 0;
}

/* Frame lexical write barrier using the specified collectable. */
MVM_STATIC_INLINE void MVM_gc_frame_lexical_write_barrier(MVMThreadContext *tc, MVMFrame *f, MVMCollectable *c) {
    if (c && !(c->flags & MVM_CF_SECOND_GEN))
        f->refs_gen2_only = 0;
}
