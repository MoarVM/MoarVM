/* Functions for if the write barriers are hit. */
MVM_PUBLIC void MVM_gc_write_barrier_hit(MVMThreadContext *tc, MVMCollectable *update_root);
MVM_PUBLIC void MVM_gc_write_barrier_hit_by(MVMThreadContext *tc, MVMCollectable *update_root,
        MVMCollectable *referenced);

/* Ensures that if a generation 2 object comes to hold a reference to a
 * nursery object, then the generation 2 object becomes an inter-generational
 * root. */
MVM_STATIC_INLINE void MVM_gc_write_barrier(MVMThreadContext *tc, MVMCollectable *update_root, MVMCollectable *referenced) {
    if (((update_root->flags & MVM_CF_SECOND_GEN) && referenced && !(referenced->flags & MVM_CF_SECOND_GEN)))
        MVM_gc_write_barrier_hit_by(tc, update_root, referenced);
}
MVM_STATIC_INLINE void MVM_gc_write_barrier_no_update_referenced(MVMThreadContext *tc, MVMCollectable *update_root, MVMCollectable *referenced) {
    if (((update_root->flags & MVM_CF_SECOND_GEN) && referenced && !(referenced->flags & MVM_CF_SECOND_GEN)))
        MVM_gc_write_barrier_hit(tc, update_root);
}

/* Does an assignment, but makes sure the write barrier MVM_WB is applied
 * first. Takes the root object, the address within it we're writing to, and
 * the thing we're writing. Note that update_addr is not involved in the
 * write barrier. */
#if MVM_GC_DEBUG
#define MVM_ASSIGN_REF(tc, update_root, update_addr, referenced) \
    { \
        void *_r = referenced; \
        if (_r && ((MVMCollectable *)_r)->owner == 0) \
            MVM_panic(1, "Invalid assignment (maybe of heap frame to stack frame?)"); \
        MVM_ASSERT_NOT_FROMSPACE(tc, _r); \
        MVM_gc_write_barrier(tc, update_root, (MVMCollectable *)_r); \
        update_addr = _r; \
    }
#else
#define MVM_ASSIGN_REF(tc, update_root, update_addr, referenced) \
    { \
        void *_r = referenced; \
        MVM_gc_write_barrier(tc, update_root, (MVMCollectable *)_r); \
        update_addr = _r; \
    }
#endif
