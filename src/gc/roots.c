#include "moarvm.h"

/* Adds a collectable object to the permanent list of GC roots, so that
 * it will always be marked and never die. */
void MVM_gc_root_add_permanent(MVMThreadContext *tc, MVMCollectable *obj) {
    /* XXX TODO */
}
