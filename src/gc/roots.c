#include "moarvm.h"

/* Adds a collectable object to the pernament list of GC roots, so that
 * it will always be marked and never die. */
void MVM_gc_root_add_pernament(MVMThreadContext *tc, MVMCollectable *obj) {
    /* XXX TODO */
}
