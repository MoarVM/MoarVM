#include "moarvm.h"

static const MVMREPROps this_repr = {
    NULL, /* type_object_for */
    NULL, /* allocate */
    NULL, /* initialize */
    NULL, /* copy_to */
    MVM_REPR_DEFAULT_ATTR_FUNCS,
    MVM_REPR_DEFAULT_BOX_FUNCS,
    MVM_REPR_DEFAULT_POS_FUNCS,
    MVM_REPR_DEFAULT_ASS_FUNCS,
    MVM_REPR_DEFAULT_ELEMS,
    NULL, /* get_storage_spec */
    NULL, /* change_type */
    NULL, /* serialize */
    NULL, /* deserialize */
    NULL, /* serialize_repr_data */
    NULL, /* deserialize_repr_data */
    NULL, /* deserialize_stable_size */
    NULL, /* gc_mark */
    NULL, /* gc_free */
    NULL, /* gc_cleanup */
    NULL, /* gc_mark_repr_data */
    NULL, /* gc_free_repr_data */
    NULL, /* compose */
    "CFPtr",
    MVM_REPR_ID_CFPtr,
    0, /* refs_frames */
};

const MVMREPROps * MVMCFPtr_initialize(MVMThreadContext *tc) {
    return &this_repr;
}
