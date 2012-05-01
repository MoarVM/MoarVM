/* The write barrier macro. Takes the current thread context, the object
 * that we are about to update and the object that it will come to reference.
 * This barrier should be called BEFORE the updated pointer is written. */
#define MVM_WB(tc, updating, referenced) \
    { \
        MVMCollectable *u = (MVMCollectable *)updating; \
        MVMCollectable *r = (MVMCollectable *)referenced; \
        if (((u->flags & MVM_CF_SECOND_GEN) && !(r->flags & MVM_CF_SECOND_GEN)) || u->sc) \
            MVM_gc_write_barrier_hit(tc, u, r);\
    }

/* Function for if the write barrier is hit. */
void MVM_gc_write_barrier_hit(MVMThreadContext *tc, MVMCollectable *updating, MVMCollectable *referenced);
