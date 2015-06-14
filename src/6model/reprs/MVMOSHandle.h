#define MVM_OSHANDLE_SYNCFILE    1
#define MVM_OSHANDLE_SYNCPIPE    2
#define MVM_OSHANDLE_SYNCSTREAM  3

/* Representation used by VM-level OS handles. */
struct MVMOSHandleBody {
    /* The function table for this handle, determining how it will process
     * various kinds of I/O related operations. */
    const MVMIOOps *ops;

    /* Any data a particular set of I/O functions wishes to store. */
    void *data;

    /* Mutex protecting access to this I/O handle. */
    uv_mutex_t *mutex;

    /* Specifies of what type data is. Can be a file, pipe or stream currently. */
    MVMuint64 type;
};
struct MVMOSHandle {
    MVMObject common;
    MVMOSHandleBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMOSHandle_initialize(MVMThreadContext *tc);
