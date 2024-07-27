#include "moar.h"

/* This representation's function pointer table. */
static const MVMREPROps CPointer_this_repr;

/* Creates a new type object of this representation, and associates it with
 * the given HOW. */
static MVMObject * type_object_for(MVMThreadContext *tc, MVMObject *HOW) {
    MVMSTable *st  = MVM_gc_allocate_stable(tc, &CPointer_this_repr, HOW);

    MVMROOT(tc, st, {
        MVMObject *obj = MVM_gc_allocate_type_object(tc, st);
        MVM_ASSIGN_REF(tc, &(st->header), st->WHAT, obj);
        st->size = sizeof(MVMCPointer);
    });

    return st->WHAT;
}

/* Compose the representation. */
static void compose(MVMThreadContext *tc, MVMSTable *st, MVMObject *info) {
}

/* Copies to the body of one object to another. */
static void copy_to(MVMThreadContext *tc, MVMSTable *st, void *src, MVMObject *dest_root, void *dest) {
    MVMCPointerBody *src_body = (MVMCPointerBody *)src;
    MVMCPointerBody *dest_body = (MVMCPointerBody *)dest;
    dest_body->ptr = src_body->ptr;
}

static void set_int(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMint64 value) {
    MVMCPointerBody *body = (MVMCPointerBody *)OBJECT_BODY(root);
#if MVM_PTR_SIZE == 4
    body->ptr = (void *)(MVMint32)value;
#else
    body->ptr = (void *)value;
#endif
}

static MVMint64 get_int(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMCPointerBody *body = (MVMCPointerBody *)OBJECT_BODY(root);
#if MVM_PTR_SIZE == 4
    return (MVMint64)(MVMint32)body->ptr;
#else
    return (MVMint64)body->ptr;
#endif
}

static void set_uint(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMuint64 value) {
    MVMCPointerBody *body = (MVMCPointerBody *)OBJECT_BODY(root);
#if MVM_PTR_SIZE == 4
    body->ptr = (void *)(MVMuint32)value;
#else
    body->ptr = (void *)value;
#endif
}

static MVMuint64 get_uint(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data) {
    MVMCPointerBody *body = (MVMCPointerBody *)OBJECT_BODY(root);
#if MVM_PTR_SIZE == 4
    return (MVMuint64)(MVMuint32)body->ptr;
#else
    return (MVMuint64)body->ptr;
#endif
}

static const MVMStorageSpec storage_spec = {
    MVM_STORAGE_SPEC_REFERENCE,       /* inlineable */
    sizeof(void *) * 8,               /* bits */
    ALIGNOF(void *),                  /* align */
    MVM_STORAGE_SPEC_BP_NONE,         /* boxed_primitive */
    0,                                /* can_box */
    0,                                /* is_unsigned */
};


/* Gets the storage specification for this representation. */
static const MVMStorageSpec * get_storage_spec(MVMThreadContext *tc, MVMSTable *st) {
    return &storage_spec;
}

static void deserialize_stable_size(MVMThreadContext *tc, MVMSTable *st, MVMSerializationReader *reader) {
    st->size = sizeof(MVMCPointer);
}

static void deserialize(MVMThreadContext *tc, MVMSTable *st, MVMObject *root, void *data, MVMSerializationReader *reader) {
    MVMCPointerBody *body = (MVMCPointerBody *)data;
    MVMint64 value;

    if (reader->root.version >= 19) {
        value = MVM_serialization_read_int(tc, reader);
    } else {
        value = MVM_serialization_read_int64(tc, reader);
    }

#if MVM_PTR_SIZE == 4
    body->ptr = (void *)(MVMuint32)value;
#else
    body->ptr = (void *)value;
#endif

}

static void serialize(MVMThreadContext *tc, MVMSTable *st, void *data, MVMSerializationWriter *writer) {
    MVMCPointerBody *body = (MVMCPointerBody *)data;
    MVM_serialization_write_int(tc, writer,
#if MVM_PTR_SIZE == 4
        (MVMuint64)(MVMuint32)body->ptr
#else
        (MVMuint64)body->ptr
#endif
    );
}

/* Initializes the representation. */
const MVMREPROps * MVMCPointer_initialize(MVMThreadContext *tc) {
    return &CPointer_this_repr;
}

static void spesh(MVMThreadContext *tc, MVMSTable *st, MVMSpeshGraph *g, MVMSpeshBB *bb, MVMSpeshIns *ins) {
    MVMuint16             opcode    = ins->info->opcode;
    switch (opcode) {
    case MVM_OP_unbox_i: {
        MVMSpeshFacts *facts = MVM_spesh_get_and_use_facts(tc, g, ins->operands[1]);
        if (facts->flags & MVM_SPESH_FACT_CONCRETE) {
            MVMSpeshOperand *orig_operands = ins->operands;
#if MVM_PTR_SIZE == 4
            ins->info = MVM_op_get_op(MVM_OP_sp_get_i32);
#else
            ins->info = MVM_op_get_op(MVM_OP_sp_get_i64);
#endif
            ins->operands = MVM_spesh_alloc(tc, g, 3 * sizeof(MVMSpeshOperand));
            ins->operands[0] = orig_operands[0];
            ins->operands[1] = orig_operands[1];
            ins->operands[2].lit_i16 = offsetof(MVMCPointer, body.ptr);
            MVM_spesh_graph_add_comment(tc, g, ins, "unbox_i into %s", ins->info->name);
        }
        else {
            MVM_spesh_graph_add_comment(tc, g, ins, "no devirt: not known concrete");
        }
        break;
    }
    default:
        MVM_spesh_graph_add_comment(tc, g, ins, "reprop %s unsupported in P6Opaque %s",
                ins->info->name,
                MVM_6model_get_stable_debug_name(tc, st));
    }
}

static const MVMREPROps CPointer_this_repr = {
    type_object_for,
    MVM_gc_allocate_object,
    NULL, /* initialize */
    copy_to,
    MVM_REPR_DEFAULT_ATTR_FUNCS,
    {
        set_int,
        get_int,
        MVM_REPR_DEFAULT_SET_NUM,
        MVM_REPR_DEFAULT_GET_NUM,
        MVM_REPR_DEFAULT_SET_STR,
        MVM_REPR_DEFAULT_GET_STR,
        set_uint,
        get_uint,
        MVM_REPR_DEFAULT_GET_BOXED_REF
    },    /* box_funcs */
    MVM_REPR_DEFAULT_POS_FUNCS,
    MVM_REPR_DEFAULT_ASS_FUNCS,
    MVM_REPR_DEFAULT_ELEMS,
    get_storage_spec,
    NULL, /* change_type */
    serialize, /* serialize */
    deserialize, /* deserialize */
    NULL, /* serialize_repr_data */
    NULL, /* deserialize_repr_data */
    deserialize_stable_size,
    NULL, /* gc_mark */
    NULL, /* gc_free */
    NULL, /* gc_cleanup */
    NULL, /* gc_mark_repr_data */
    NULL, /* gc_free_repr_data */
    compose,
    spesh,
    "CPointer", /* name */
    MVM_REPR_ID_MVMCPointer,
    NULL, /* unmanaged_size */
    NULL, /* describe_refs */
};
