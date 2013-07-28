/* Lexotics are involved in the implementation of control structures such as
 * return. */

typedef struct _MVMLexoticBody {
    /* The frame to unwind to. */
    MVMFrame *frame;

    /* The frame handler to unwind to. */
    MVMFrameHandler *handler;

    /* The result object. */
    MVMObject *result;
} MVMLexoticBody;

typedef struct _MVMLexotic {
    MVMObject common;
    MVMLexoticBody body;
} MVMLexotic;

/* Function for REPR setup. */
MVMREPROps * MVMLexotic_initialize(MVMThreadContext *tc);
