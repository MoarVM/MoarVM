/* Representation used by Uninstantiables. */
struct MVMUninstantiable {
    MVMObject common;
};

/* Function for REPR setup. */
const MVMREPROps * MVMUninstantiable_initialize(MVMThreadContext *tc);
