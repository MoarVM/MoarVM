/* Representation used by P6 nums. */
typedef struct _P6numBody {
    /* Float storage slot. */
    MVMnum64 value;
} P6numBody;
typedef struct _P6num {
    MVMObject common;
    P6numBody body;
} P6num;

/* Function for REPR setup. */
MVMREPROps * P6num_initialize(MVMThreadContext *tc);
