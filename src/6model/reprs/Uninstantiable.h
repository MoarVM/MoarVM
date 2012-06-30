/* Representation used by Uninstantiables. */
typedef struct _Uninstantiable {
    MVMObject common;
} Uninstantiable;

/* Function for REPR setup. */
MVMREPROps * Uninstantiable_initialize(MVMThreadContext *tc);
