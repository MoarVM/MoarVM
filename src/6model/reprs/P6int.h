/* Representation used by P6 native ints. */
struct MVMP6intBody {
    /* Integer storage slot. */
    MVMint64 value;
};
struct MVMP6int {
    MVMObject common;
    MVMP6intBody body;
};

/* The bit width requirement is shared for all instances of the same type. */
struct MVMP6intREPRData {
    MVMint16 bits;
    MVMint16 is_unsigned;
};

/* Function for REPR setup. */
const MVMREPROps * MVMP6int_initialize(MVMThreadContext *tc);
