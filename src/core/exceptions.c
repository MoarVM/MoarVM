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

/* Throws an ad-hoc (untyped) formatted exception with an apr error appended. */
MVM_NO_RETURN
void MVM_exception_throw_apr_error(MVMThreadContext *tc, apr_status_t code, const char *messageFormat, ...) {
    /* XXX Well, need to implement exceptions. So for now just mimic panic. */
    char *error_string = malloc(512);
    int offset;
    va_list args;
    va_start(args, messageFormat);
    
    /* inject the supplied formatted string */
    offset = vsprintf(error_string, messageFormat, args);
    va_end(args);
    
    /* append the apr error */
    apr_strerror(code, error_string + offset, 512 - offset);
    fwrite(error_string, 1, strlen(error_string), stderr);
    free(error_string);
    exit(1);
}
