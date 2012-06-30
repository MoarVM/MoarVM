/* Representation used by P6 ints. */
typedef struct _P6intBody {
    /* Integer storage slot. */
    MVMint64 value;
} P6intBody;
typedef struct _P6int {
    MVMObject common;
    P6intBody body;
} P6int;

/* Function for REPR setup. */
MVMREPROps * P6int_initialize(MVMThreadContext *tc);
