#include "tommath.h"

/* Representation used by P6 Ints. */
typedef struct _P6bigintBody {
    /* Big integer storage slot. */
    mp_int i;
} P6bigintBody;
typedef struct _P6bigint {
    MVMObject common;
    P6bigintBody body;
} P6bigint;

/* Function for REPR setup. */
MVMREPROps * P6bigint_initialize(MVMThreadContext *tc);
