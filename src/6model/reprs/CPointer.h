/* Representation for C Pointer. */
struct MVMCPointerBody {
    void *ptr;
};

struct MVMCPointer {
    MVMObject common;
    MVMCPointerBody body;
};

/* Initializes the CPointer REPR. */
const MVMREPROps * MVMCPointer_initialize(MVMThreadContext *tc);
