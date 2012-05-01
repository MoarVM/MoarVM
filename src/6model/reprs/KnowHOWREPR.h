/* Representation used for bootstrapping the KnowHOW type. */
typedef struct _MVMKnowHOWREPRBody {
    /* Methods table; a hash. */
    MVMObject *methods;

    /* Array of attribute meta-objects. */
    MVMObject *attributes;

    /* Name of the type. */
    MVMString *name;
} MVMKnowHOWREPRBody;
typedef struct _MVMKnowHOWREPR {
    MVMObject common;
    MVMKnowHOWREPRBody body;
} MVMKnowHOWREPR;

/* Function for REPR setup. */
MVMREPROps * MVMKnowHOWREPR_initialize(MVMThreadContext *tc);
