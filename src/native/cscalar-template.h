#include "moarvm.h"

#define EVAL(MACRO) DO_EVAL(MACRO, REPR_NAME)
#define DO_EVAL(MACRO, ...) MACRO(__VA_ARGS__)
#define INIT(NAME) MVM ## NAME ## _initialize

static const MVMREPROps this_repr;

const MVMREPROps * EVAL(INIT)(MVMThreadContext *tc) {
    return &this_repr;
}
