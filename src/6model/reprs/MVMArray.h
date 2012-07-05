/* Representation used by VM-level arrays. Adopted from QRPA work by
 * Patrick Michaud. */
typedef struct _MVMArrayBody {
    MVMuint64   elems;        /* number of elements */
    MVMuint64   start;        /* slot index of first element */
    MVMuint64   ssize;        /* size of slots array */
    MVMObject **slots;        /* array of PMC slots */
} MVMArrayBody;
typedef struct _MVMArray {
    MVMObject common;
    MVMArrayBody body;
} MVMArray;

/* Function for REPR setup. */
MVMREPROps * MVMArray_initialize(MVMThreadContext *tc);
