/* Representation used for bootstrapping attributes. */
typedef struct _MVMKnowHOWAttributeREPRBody {
    /* The attribute's name. */
    MVMString *name;

    /* The attribute's type. */
    MVMObject *type;

    /* Whether the attribute serves as a box target. */
    MVMuint32 box_target;
} MVMKnowHOWAttributeREPRBody;
typedef struct _MVMKnowHOWAttributeREPR {
    MVMObject common;
    MVMKnowHOWAttributeREPRBody body;
} MVMKnowHOWAttributeREPR;

/* Function for REPR setup. */
MVMREPROps * MVMKnowHOWAttributeREPR_initialize(MVMThreadContext *tc);
