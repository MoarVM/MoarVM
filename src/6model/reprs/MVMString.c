#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMString);
    });

    return st->WHAT;
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMStringBody *src_body  = (MVMStringBody *)src;
    MVMStringBody *dest_body = (MVMStringBody *)dest;
    dest_body->codes  = src_body->codes;
    dest_body->flags  = src_body->flags;
    switch(src_body->flags & MVM_STRING_TYPE_MASK) {
        case MVM_STRING_TYPE_INT32:
            if ((dest_body->graphs = src_body->graphs)) {
                dest_body->int32s = malloc(sizeof(MVMCodepoint32) * dest_body->graphs);
                memcpy(dest_body->int32s, src_body->int32s, sizeof(MVMCodepoint32) * src_body->graphs);
            }
            break;
        case MVM_STRING_TYPE_UINT8:
            if ((dest_body->graphs = src_body->graphs)) {
                dest_body->uint8s = malloc(sizeof(MVMCodepoint8) * dest_body->graphs);
                memcpy(dest_body->uint8s, src_body->uint8s, sizeof(MVMCodepoint8) * src_body->graphs);
            }
            break;
        case MVM_STRING_TYPE_ROPE: {
            MVMStrandIndex strand_count = dest_body->num_strands = src_body->num_strands;
            if (strand_count) {
                dest_body->strands = malloc(sizeof(MVMStrand) * (strand_count + 1));
                memcpy(dest_body->strands, src_body->strands, sizeof(MVMStrand) * (strand_count + 1));
            }
            break;
        }
        default:
            MVM_exception_throw_adhoc(tc, "internal string corruption");
    }
}

/* Adds held objects to the GC worklist. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMStringBody *body  = (MVMStringBody *)data;
    if ((body->flags & MVM_STRING_TYPE_MASK) == MVM_STRING_TYPE_ROPE) {
        MVMStrand *strands = body->strands;
        MVMStrandIndex index = 0;
        MVMStrandIndex strand_count = body->num_strands;
        while(index < strand_count)
            MVM_gc_worklist_add(tc, worklist, &(strands + index++)->string);
    }
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMString *str = (MVMString *)obj;
    MVM_checked_free_null(str->body.storage);
    str->body.graphs = str->body.codes = str->body.flags = 0;
}

/* Gets the storage specification for this representation. */
static MVMStorageSpec get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    MVMStorageSpec spec;
    spec.inlineable      = MVM_STORAGE_SPEC_REFERENCE;
    spec.boxed_primitive = MVM_STORAGE_SPEC_BP_NONE;
    spec.can_box         = 0;
    return spec;
}

/* Compose the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info) {
    /* Nothing to do for this REPR. */
}

/* Initializes the representation. */
const MVMREPROps * MVMString_initialize(MVMThreadContext *tc) {
    return &this_repr;
}

static const MVMREPROps this_repr = {
    type_object_for,
    MVM_gc_allocate_object,
    NULL, /* initialize */
    copy_to,
    MVM_REPR_DEFAULT_ATTR_FUNCS,
    MVM_REPR_DEFAULT_BOX_FUNCS,
    MVM_REPR_DEFAULT_POS_FUNCS,
    MVM_REPR_DEFAULT_ASS_FUNCS,
    MVM_REPR_DEFAULT_ELEMS,
    get_storage_spec,
    NULL, /* change_type */
    NULL, /* serialize */
    NULL, /* deserialize */
    NULL, /* serialize_repr_data */
    NULL, /* deserialize_repr_data */
    NULL, /* deserialize_stable_size */
    gc_mark,
    gc_free,
    NULL, /* gc_cleanup */
    NULL, /* gc_mark_repr_data */
    NULL, /* gc_free_repr_data */
    compose,
    "MVMString", /* name */
    MVM_REPR_ID_MVMString,
    0, /* refs_frames */
};