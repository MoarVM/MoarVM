#include <moarvm.h>

/* Called when the write barrier macro detects we need to trigger
 * the write barrier. Arguments are the same as to the barrier macro
 * itself (updating is the object that we're about to write a pointer
 * into, and referenced is the object that the pointer references). */
void MVM_gc_write_barrier_hit(MVMThreadContext *tc, MVMCollectable *update_root,
                              MVMCollectable **update_addr, MVMCollectable *referenced) {
    /* Old generation object pointing to new? */
    if ((update_root->flags & MVM_CF_SECOND_GEN) && !(referenced->flags & MVM_CF_SECOND_GEN)) {
        MVM_gc_root_gen2_add(tc, update_addr);
    }
    
    /* Object being updated is in an SC? */
    if (update_root->sc) {
        /* XXX Trigger any needed SC move logic. */
    }
    
    /* XXX Probably some thread local invariant needs enforcing here too. */
}
