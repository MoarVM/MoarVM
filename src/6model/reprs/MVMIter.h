/* Representation used by VM-level arrays. Adopted from QRPA work by
 * Patrick Michaud. */
typedef struct _MVMIterBody {
    MVMuint64   elems;        /* number of elements */
    MVMuint64   start;        /* slot index of first element */
    MVMuint64   ssize;        /* size of slots array */
    MVMObject **slots;        /* array of PMC slots */
} MVMIterBody;
typedef struct _MVMIter {
    MVMObject common;
    MVMIterBody body;
} MVMIter;

/* Function for REPR setup. */
MVMREPROps * MVMIter_initialize(MVMThreadContext *tc);
