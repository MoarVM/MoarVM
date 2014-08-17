/* Representation used by P6 nums. */
struct MVMP6numBody {
    /* Float storage slot. */
    union {
        MVMnum64 n64;
        MVMnum32 n32;
    } value;
};
struct MVMP6num {
    MVMObject common;
    MVMP6numBody body;
};

/* The bit width requirement is shared for all instances of the same type. */
struct MVMP6numREPRData {
    MVMint16       bits;
    MVMStorageSpec storage_spec;
};

/* Function for REPR setup. */
const MVMREPROps * MVMP6num_initialize(MVMThreadContext *tc);
