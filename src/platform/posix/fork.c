#include "moar.h"
#include <unistd.h>

MVMint64 MVM_platform_supports_fork(MVMThreadContext *tc) {
    return 1;
}

MVMint64 MVM_platform_fork(MVMThreadContext *tc) {
    return fork();
}
