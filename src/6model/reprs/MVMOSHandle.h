/* Representation used by VM-level OS handles. */
struct MVMOSHandleBody {
    /* The function table for this handle, determining how it will process
     * various kinds of I/O related operations. */
    const MVMIOOps *ops;

    /* Any data a particular set of I/O functions wishes to store. */
    void *data;
};
struct MVMOSHandle {
    MVMObject common;
    MVMOSHandleBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMOSHandle_initialize(MVMThreadContext *tc);
