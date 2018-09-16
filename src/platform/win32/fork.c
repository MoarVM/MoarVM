#include "moar.h"

MVMint64 MVM_platform_supports_fork(MVMThreadContext *tc) {
    return 0;
}

/* Windows does not support fork() */
MVMint64 MVM_platform_fork(MVMThreadContext *tc) {
    MVM_exception_throw_adhoc(tc, "This platform does not support fork()");
}
