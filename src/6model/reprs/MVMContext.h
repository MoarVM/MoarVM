/* Representation for a context in the VM. Holds an MVMFrame. */
typedef struct _MVMContextBody {
    MVMFrame *context;
} MVMContextBody;
typedef struct _MVMContext {
    MVMObject common;
    MVMContextBody body;
} MVMContext;

/* Function for REPR setup. */
MVMREPROps * MVMContext_initialize(MVMThreadContext *tc);
