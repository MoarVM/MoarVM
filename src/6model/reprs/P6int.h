/* Representation used by P6 native ints. */
struct MVMP6intBody {
    /* Integer storage slot. */
    MVMint64 value;
};
struct MVMP6int {
    MVMObject common;
    MVMP6intBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMP6int_initialize(MVMThreadContext *tc);
