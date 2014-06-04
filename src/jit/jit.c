#include "moar.h"
#ifndef _WIN32
#include <sys/mman.h>
#endif
#include <dasm_proto.h>

MVMJitGraph * MVM_jit_try_make_jit_graph(MVMThreadContext *tc, MVMSpeshGraph *spesh) {
    /* can't jit right now */
    return NULL;
}


