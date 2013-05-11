/* The write barrier macro for updating a reference directly in an object body
 * or in memory that we can promise will never move during the lifetime of the
 * owning object. Takes the current thread context, the object that we are about
 * to update, the address that will contain the reference to the nursery object
 * and the object that it will come to reference. This barrier should be called
 * BEFORE the updated pointer is written. */
#define MVM_WB_REF(tc, update_root, update_addr, referenced) \
    { \
        MVMCollectable *u = (MVMCollectable *)update_root; \
        MVMCollectable *r = (MVMCollectable *)referenced; \
        if (((u->flags & MVM_CF_SECOND_GEN) && r && !(r->flags & MVM_CF_SECOND_GEN)) || u->sc) \
            MVM_gc_write_ref_barrier_hit(tc, u, (MVMCollectable **)&(update_addr), r); \
    }

/* Does an assignment, but makes sure the write barrier MVM_WB_REF is applied
 * first. Takes the root object, the address within it we're writing to, and
 * the thing we're writing. */
#define MVM_ASSIGN_REF(tc, update_root, update_addr, referenced) \
    { \
        void **_u = &(update_addr); \
        void *_r = referenced; \
        MVM_WB_REF(tc, update_root, *_u, _r); \
        *_u = _r; \
    }

/* The write barrier macro for updating a reference held by some kind of aggregate
 * that may re-arrange its (unmanaged) memory over its lifetime. Takes the current
 * thread context, the object that we are about to update and the object that it
 * will come to reference. This barrier should be called BEFORE the updated pointer
 * is written. */
#define MVM_WB_AGG(tc, update_root, referenced) \
    { \
        MVMCollectable *u = (MVMCollectable *)update_root; \
        MVMCollectable *r = (MVMCollectable *)referenced; \
        if (((u->flags & MVM_CF_SECOND_GEN) && r && !(r->flags & MVM_CF_SECOND_GEN)) || u->sc) \
            MVM_gc_write_agg_barrier_hit(tc, u, r); \
    }

/* Does an assignment, but makes sure the write barrier MVM_WB_AGG is applied
 * first. Takes the root object, the address within it we're writing to, and
 * the thing we're writing. Since an aggregate manages its own memory, the
 * update_addr places no role in the write barrier. */
#define MVM_ASSIGN_AGG(tc, update_root, update_addr, referenced) \
    { \
        void *_r = referenced; \
        MVM_WB_AGG(tc, update_root, _r); \
        update_addr = _r; \
    }

/* Functions for if the write barriers are hit. */
void MVM_gc_write_ref_barrier_hit(MVMThreadContext *tc, MVMCollectable *update_root, MVMCollectable **update_addr, MVMCollectable *referenced);
void MVM_gc_write_agg_barrier_hit(MVMThreadContext *tc, MVMCollectable *update_root, MVMCollectable *referenced);
