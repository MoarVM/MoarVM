#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps MVMString_this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st = MVM_gc_allocate_stable(tc, &MVMString_this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMString);
    });

    return st->WHAT;
}

/* Copies the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMStringBody *src_body     = (MVMStringBody *)src;
    MVMStringBody *dest_body    = (MVMStringBody *)dest;
    dest_body->storage_type     = src_body->storage_type;
    dest_body->num_strands      = src_body->num_strands;
    dest_body->num_graphs       = src_body->num_graphs;
    dest_body->cached_hash_code = src_body->cached_hash_code;
    switch (dest_body->storage_type) {
        case MVM_STRING_GRAPHEME_32:
            if (dest_body->num_graphs) {
                dest_body->storage.blob_32 = MVM_malloc(dest_body->num_graphs * sizeof(MVMGrapheme32));
                memcpy(dest_body->storage.blob_32, src_body->storage.blob_32,
                    dest_body->num_graphs * sizeof(MVMGrapheme32));
            }
            break;
        case MVM_STRING_GRAPHEME_ASCII:
        case MVM_STRING_GRAPHEME_8:
            if (dest_body->num_graphs) {
                dest_body->storage.blob_8 = MVM_malloc(dest_body->num_graphs);
                memcpy(dest_body->storage.blob_8, src_body->storage.blob_8,
                    dest_body->num_graphs);
            }
            break;
        case MVM_STRING_STRAND:
            dest_body->storage.strands = MVM_malloc(dest_body->num_strands * sizeof(MVMStringStrand));
            memcpy(dest_body->storage.strands, src_body->storage.strands,
                dest_body->num_strands * sizeof(MVMStringStrand));
            break;
        case MVM_STRING_IN_SITU_8:
        case MVM_STRING_IN_SITU_32:
            memcpy(dest_body->storage.in_situ_8, src_body->storage.in_situ_8,
                src_body->num_graphs * sizeof(MVMGrapheme8));
            break;
        default:
            MVM_exception_throw_adhoc(tc, "Internal string corruption");
    }
}

/* Adds held objects to the GC worklist. */
static void gc_mark(MVMThreadContext *tc, MVMSTable *st, void *data, MVMGCWorklist *worklist) {
    MVMStringBody *body = (MVMStringBody *)data;
    if (body->storage_type == MVM_STRING_STRAND) {
        MVMStringStrand *strands = body->storage.strands;
        MVMuint16 i;
        for (i = 0; i < body->num_strands; i++)
            MVM_gc_worklist_add(tc, worklist, &(strands[i].blob_string));
    }
}

/* Called by the VM in order to free memory associated with this object. */
static void gc_free(MVMThreadContext *tc, MVMObject *obj) {
    MVMString *str = (MVMString *)obj;
    if (str->body.storage_type != MVM_STRING_IN_SITU_8 && str->body.storage_type != MVM_STRING_IN_SITU_32)
        MVM_free(str->body.storage.any);
    str->body.num_graphs = str->body.num_strands = 0;
}

static const MVMStorageSpec storage_spec = {
    MVM_STORAGE_SPEC_REFERENCE, /* inlineable */
    0,                          /* bits */
    0,                          /* align */
    MVM_STORAGE_SPEC_BP_NONE,   /* boxed_primitive */
    0,                          /* can_box */
    0,                          /* is_unsigned */
};

/* Gets the storage specification for this representation. */
static const MVMStorageSpec * get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    return &storage_spec;
}

/* Compose the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info) {
    /* Nothing to do for this REPR. */
}

/* Calculates the non-GC-managed memory we hold on to. */
static MVMuint64 unmanaged_size(MVMThreadContext *tc, MVMSTable *st, void *data) {
    MVMStringBody *body = (MVMStringBody *)data;
    switch (body->storage_type) {
        case MVM_STRING_GRAPHEME_32:
            return sizeof(MVMGrapheme32) * body->num_graphs;
        case MVM_STRING_STRAND:
            return sizeof(MVMStringStrand) * body->num_strands;
        default:
            return body->num_graphs;
    }
}

/* Initializes the representation. */
const MVMREPROps * MVMString_initialize(MVMThreadContext *tc) {
    return &MVMString_this_repr;
}

static const MVMREPROps MVMString_this_repr = {
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
    NULL, /* spesh */
    "MVMString", /* name */
    MVM_REPR_ID_MVMString,
    unmanaged_size,
    NULL, /* describe_refs */
};
