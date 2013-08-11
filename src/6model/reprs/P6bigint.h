#include "tommath.h"

/* Representation used by P6 Ints. */
struct MVMP6bigintBody {
    /* Big integer storage slot. */
    mp_int i;
};
struct MVMP6bigint {
    MVMObject common;
    MVMP6bigintBody body;
};

/* Function for REPR setup. */
MVMREPROps * MVMP6bigint_initialize(MVMThreadContext *tc);
