#include "moarvm.h"
#include <stdarg.h>

/* Panics and shuts down the VM. Don't do this unless it's something quite
 * unrecoverable.
 * TODO: Some hook for embedders.
 */
MVM_NO_RETURN
void MVM_panic(MVMint32 exitCode, const char *messageFormat, ...) {
    va_list args;
    va_start(args, messageFormat);
    vfprintf(stderr, messageFormat, args);
    va_end(args);
    exit(exitCode);
}

/* Throws an ad-hoc (untyped) exception. */
MVM_NO_RETURN
void MVM_exception_throw_adhoc(MVMThreadContext *tc, const char *messageFormat, ...) {
    /* XXX Well, need to implement exceptions. So for now just mimic panic. */
    va_list args;
    va_start(args, messageFormat);
    vfprintf(stderr, messageFormat, args);
    va_end(args);
    exit(1);
}
