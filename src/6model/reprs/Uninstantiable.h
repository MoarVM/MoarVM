/* Representation used by Uninstantiables. */
struct MVMUninstantiable {
    MVMObject common;
};

/* Function for REPR setup. */
MVMREPROps * MVMUninstantiable_initialize(MVMThreadContext *tc);
