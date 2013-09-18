/* Representation used by P6 nums. */
struct MVMP6numBody {
    /* Float storage slot. */
    MVMnum64 value;
};
struct MVMP6num {
    MVMObject common;
    MVMP6numBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMP6num_initialize(MVMThreadContext *tc);
