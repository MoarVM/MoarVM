/* Representation used by the null REPR. */
struct MVMNull {
    MVMObject common;
};

/* Function for REPR setup. */
const MVMREPROps * MVMNull_initialize(MVMThreadContext *tc);
