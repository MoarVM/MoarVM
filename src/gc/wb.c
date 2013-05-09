#include <moarvm.h>

/* Called when the write barrier macro detects we need to trigger
 * the write barrier. Arguments are the same as to the barrier macro
 * itself (updating is the object that we're about to write a pointer
 * into, and referenced is the object that the pointer references).
 * The update_addr is the address that will contain the reference
 * once the write is done. See the conditions on the barrier for when
 * this is an OK thing to do. */
void MVM_gc_write_ref_barrier_hit(MVMThreadContext *tc, MVMCollectable *update_root,
        MVMCollectable **update_addr, MVMCollectable *referenced) {
    /* Old generation object pointing to new? */
    if ((update_root->flags & MVM_CF_SECOND_GEN) && referenced && !(referenced->flags & MVM_CF_SECOND_GEN)) {
        MVM_gc_root_gen2_ref_add(tc, update_addr);
    }
    
    /* Object being updated is in an SC? */
    if (update_root->sc) {
        /* XXX Trigger any needed SC move logic. */
    }
}

/* Called when the write barrier macro detects we need to trigger
 * the write barrier. Arguments are the same as to the barrier macro
 * itself (updating is the object that we're about to write a pointer
 * into, and referenced is the object that the pointer references).
 * This barrier forces a re-scan of the object's contents during a GC
 * run - even a nursery only one - since somewhere it has references
 * to a nursery object. This means the object's REPR is free to do any
 * re-organization of its unmanaged memory, which is what VMArray and
 * VMHash will want to do as they grow. */
void MVM_gc_write_agg_barrier_hit(MVMThreadContext *tc, MVMCollectable *update_root,
        MVMCollectable *referenced) {
    /* Old generation aggregate pointing to nursery object? */
    if ((update_root->flags & MVM_CF_SECOND_GEN) && referenced && !(referenced->flags & MVM_CF_SECOND_GEN)) {
        MVM_gc_root_gen2_agg_add(tc, (MVMObject *)update_root);
    }
    
    /* Object being updated is in an SC? */
    if (update_root->sc) {
        /* XXX Trigger any needed SC move logic. */
    }
}
