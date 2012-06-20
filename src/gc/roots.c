#include "moarvm.h"

/* Adds a location holding a collectable object to the permanent list of GC
 * roots, so that it will always be marked and never die. Note that the
 * address of the collectable must be passed, since it will need to be
 * updated. */
void MVM_gc_root_add_permanent(MVMThreadContext *tc, MVMCollectable **obj) {
    /* XXX TODO */
}
