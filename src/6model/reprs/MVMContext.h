/* Representation for a context in the VM. Holds an MVMFrame. */
struct MVMContextBody {
    MVMFrame *context;
};
struct MVMContext {
    MVMObject common;
    MVMContextBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMContext_initialize(MVMThreadContext *tc);
