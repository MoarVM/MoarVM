/* Lexotics are involved in the implementation of control structures such as
 * return. */

struct MVMLexoticBody {
    /* The target frame type. */
    MVMStaticFrame *sf;

    /* The result object. */
    MVMObject *result;

    /* Index of the frame handler to use. */
    MVMint32 handler_idx;
};

struct MVMLexotic {
    MVMObject common;
    MVMLexoticBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMLexotic_initialize(MVMThreadContext *tc);
