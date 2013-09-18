/* Representation holding a pointer to a C function, which is
 * passed a callsite descriptor and an argument list as well as
 * the current thread context. Used for the handful of things
 * that are implemented as C functions inside the VM. */
struct MVMCFunctionBody {
    void (*func) (MVMThreadContext *tc, MVMCallsite *callsite,
        MVMRegister *args);
};
struct MVMCFunction {
    MVMObject common;
    MVMCFunctionBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMCFunction_initialize(MVMThreadContext *tc);
