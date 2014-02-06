#include "tommath.h"

/* Representation used by big integers; inlined into P6bigint. */
struct MVMP6bigintBody {
    /* Pointer to a libtommath big integer. */
    mp_int *bigint;
};
struct MVMP6bigint {
    MVMObject common;
    MVMP6bigintBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMP6bigint_initialize(MVMThreadContext *tc);
