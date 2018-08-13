#include <moar.h>

/* Called when the write barrier macro detects we need to trigger
 * the write barrier. Arguments are the same as to the barrier macro
 * itself (updating is the object that we're about to write a pointer
 * into, and referenced is the object that the pointer references).
 * This barrier forces a re-scan of the object's contents during a GC
 * run - even a nursery only one - since somewhere it has references
 * to a nursery object. */
void MVM_gc_write_barrier_hit(MVMThreadContext *tc, MVMCollectable *update_root) {
    if (!(update_root->flags & MVM_CF_IN_GEN2_ROOT_LIST))
        MVM_gc_root_gen2_add(tc, update_root);
}
void MVM_gc_write_barrier_hit_by(MVMThreadContext *tc, MVMCollectable *update_root,
                                 MVMCollectable *referenced) {
    if (!(update_root->flags & MVM_CF_IN_GEN2_ROOT_LIST))
        MVM_gc_root_gen2_add(tc, update_root);
    referenced->flags |= MVM_CF_REF_FROM_GEN2;
}
