/* Representation used for bootstrapping attributes. */
struct MVMKnowHOWAttributeREPRBody {
    /* The attribute's name. */
    MVMString *name;

    /* The attribute's type. */
    MVMObject *type;

    /* Whether the attribute serves as a box target. */
    MVMuint32 box_target;
};
struct MVMKnowHOWAttributeREPR {
    MVMObject common;
    MVMKnowHOWAttributeREPRBody body;
};

/* Function for REPR setup. */
const MVMREPROps * MVMKnowHOWAttributeREPR_initialize(MVMThreadContext *tc);
