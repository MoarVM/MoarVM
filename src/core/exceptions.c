#include "moarvm.h"

/* Panics and shuts down the VM. Don't do this unless it's something quite
 * unrecoverable.
 * TODO: Some hook for embedders.
 */
void MVM_panic(char *reason) {
    fprintf(stderr, reason);
    exit(1);
}

/* Throws an ad-hoc (untyped) exception. */
void MVM_exception_throw_adhoc(MVMThreadContext *tc, const char *message) {
    /* XXX Well, need to implement exceptions. So for now just panic. */
    MVM_panic(message);
}
