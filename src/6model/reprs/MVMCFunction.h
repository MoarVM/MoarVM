/* Representation holding a pointer to a C function, which is
 * passed a callsite descriptor and an argument list as well as
 * the current thread context. Used for the handful of things
 * that are implemented as C functions inside the VM. */
typedef struct _MVMCFunctionBody {
    void (*func) (struct _MVMThreadContext *tc, struct _MVMCallsite *callsite,
        union _MVMArg *args);
} MVMCFunctionBody;
typedef struct _MVMCFunction {
    MVMObject common;
    MVMCFunctionBody body;
} MVMCFunction;

/* Function for REPR setup. */
MVMREPROps * MVMCFunction_initialize(MVMThreadContext *tc);
