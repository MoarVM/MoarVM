/* Lexotics are involved in the implementation of control structures such as
 * return. */

struct MVMLexoticBody {
    /* The frame to unwind to. */
    MVMFrame *frame;

    /* The frame handler to unwind to. */
    MVMFrameHandler *handler;

    /* The result object. */
    MVMObject *result;
};

struct MVMLexotic {
    MVMObject common;
    MVMLexoticBody body;
};

/* Function for REPR setup. */
MVMREPROps * MVMLexotic_initialize(MVMThreadContext *tc);
